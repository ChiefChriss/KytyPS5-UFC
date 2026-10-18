# KytyPS5-UFC — handoff for the automated loop

## CURRENT STATE — read this first (updated 2026-10-07)

**Installed build: Dev 124. Full CTest 56/56. Newest entry below is the current iteration.**

### Do these first

1. **Fix the one proven defect.** `TextureCache::DownloadImageMemory` (`textureCache.cpp:2002-2005`)
   publishes new guest backing from a deferred priority callback without making resident native
   buffers coherent. A failing standalone fixture reproduces it (`--image-backing-bda-only`,
   deliberately NOT enrolled in the normal suite until corrected). Report:
   `_Build/agent-reports/dev124-image-publication-regression-20261007.md`.

   Ordering is the easy thing to get wrong:
   - **Mark-dirty-then-write is wrong.** A concurrent BDA sync in that window uploads the stale
     backing and clears the dirty bit, making it permanently stale.
   - **Write the backing, then a no-flush CPU-dirty transition** (`ChangeState<DirtySource::Cpu>`
     + `RecordCpuDirty`, the non-GPU-dirty branch of `InvalidateRegion`, `memoryTracker.h:50`). A
     sync racing the gap re-uploads identical correct bytes; the re-dirty costs one redundant upload.
   - **Never call `InvalidateMemory` in the callback.** It flushes via `ReadMemory`, which `EXIT`s
     for deferred operations (`bufferCache.cpp:354`). This is why the fixture fails today.
   - Keep any new path opt-in/OFF. Add pending-generation and newer-writer guards, then enroll the
     fixture as a normal CTest.

2. **Freeze new diagnostics.** Dev 119-124 added NaN tracing, checkpoint provenance and publication
   fixtures and produced attribution but **zero** corruption fixes. Do not add more instrumentation
   to a path already known to be broken. Gate every soak on a cheap deterministic fixture test first.

3. **Treat the user's manual check as a gate.** The manual ground-and-pound / stretched-mesh check
   has been outstanding for 40+ iterations. It is the only signal that can confirm a visual fix.

### Correcting a stale review

`../LATEST-REVIEW.md` was written against **Dev 088** and presents two defects as live that are
**fixed** in Dev 124 (fault-bitmap atomicity; the 1023-slot over-read). It now carries a status
table at the top. Don't re-port those.

### Levers already tried and rejected — read the entry before re-proposing

- **SRT memoization**: 2.07% -> 3.24% hit rate at 64 slots, `shadow_mismatch=0`. Per-draw SRT is
  irreducible. Raw evidence retained in `_Build/autolab/d095-srtmemo*`.
- **BDA dirty-log**: **ON BY DEFAULT** in the installed build (`bufferCache.cpp:53` uses
  `UfcSwitch`, i.e. `=0` turns it off) — despite comments above it still calling it opt-in, and
  despite `ENGINE-REVIEW.md` claiming it is OFF. It was blamed for mesh explosions (Dev 073) and
  the fast path covers ~97-99% of syncs. **This is a live suspect, not a settled lever.** The
  `BDA_SYNC_GENERATION` variant (`bufferCache.cpp:77`) is genuinely opt-in/OFF.
- **GPU-sourced indirect draws**, **GPU DCC / fill-convert**, **relaxed readback** (banned: stale data).
- **30 FPS is not reachable by the current approach.** Fight FPS has sat at 17-20 across every lever
  tried, and the A/B runs are phase-confounded enough that they cannot resolve a real gain.
  Correctness first; revisit perf with clean baselines.

### Note on the attribution method currently being proposed

A plan to "identify the actual failing shader" via Ghidra + SPIR-V diffing against the eboot assumes
a shader is at fault. The defect above produces **correct shaders fed stale vertex data**, and the
dominant symptom is stretched geometry with an intact HUD and arena — which shader-level analysis
cannot see. Async readback (a separate perf proposal) is the same class of ordering problem as #1
and carries the same race surface.

---

## Latest local iteration

### UFC Dev 124 — real image/backing publication defect reproduced; existing 56/56 (2026-10-07, 02:28)

One bounded **failing regression milestone**, not an engine correction/FPS gain. Engine,
installed build124, defaults and prior uncommitted work unchanged. Added standalone
`CheckImageBackingBdaPublication` / `--image-backing-bda-only` in
`tests/ShaderRecompilerComputeTests.cpp`. Real native image clear -> actual deferred
`DownloadImageMemory` -> exact completed priority tick -> existing cached native buffer.
No dropped-log fabrication, backing poisoning, skipped work or ObtainBuffer refresh.

Worker `oc20261007-022425-tests` focused temporary CTest **0/1,3.27s** reaches exact
defect after successful teardown: backing/image-copy **55667788**, CPUdirty**false**,
dirty-log and forced-full-walk native **11223344**, neighbor11223344, real CPU-invalidated
control **55667789**. **Full walk also fails: backing changes bypass buffer dirty bits**,
not merely missing dirty-log entries. Native transfer read, not translated BDA shader
or live UFC culprit proof. Initial fixture union-constructor error fixed, no engine edit.

Known-failing standalone fixture retained, **NOT enrolled in normal suite until correction**;
temporary CMake registration removed, no net cycle CMake edit. Full worker
`oc20261007-022612-tests` **56/56,29.39s** explicitly EXCLUDES reproducer; scoped diff-check
passes. No numbered build/install or new fight: unchanged-engine soak cannot verify a fix.
No scripts/config/agents/commits/pushes. Ownership/env/heartbeat checked before each job.

Reused control `_Build/autolab/d124-publication-control-framedump-b-20261007-020916`:
first95.48,observed324.99,6690frames/**20.68FPS**,opening45**17.08**,median/min19/14,
RSS11183.3/VRAM12828MB,fgFalse,late waits cp84.92/other94.75/untagged7.07/write0.79ms/s.
Not new acceptance; prior124A/122 positive stretching stands, no new visual observation,
manual transient check missing. **Retain reproducer only; no corrected20/25/30/media claim**.
Fresh06:24 advisory's30min unchanged soak/55tests advice rejected in favor of actual defect.

Next: correct `DownloadImageMemory` backing-to-resident-buffer coherence with ordered native
publication or atomic completion transaction. Prove pending/newer CPU/GPU writes, mixed-page
ownership, padding/lifetimes and BDA/indirect-copy epochs. Do NOT just invalidate in the
priority callback (GPU-dirty neighbor readback is forbidden there). Register fixture normally
with correction, full57+ and worker gameplay then. Report/replay registration:
`_Build/agent-reports/dev124-image-publication-regression-20261007.md`.

### UFC Dev 124 — opt-in BDA generation/log composition corrected; 56/56; visual acceptance fails (2026-10-07, 02:18)

One bounded engine correction, **not a mesh/blackout fix or FPS gain**. Preserved prior
uncommitted work/defaults. Changed cycle portions of `renderer/cache/bufferCache.cpp`,
`tests/ShaderRecompilerComputeTests.cpp`, `CMakeLists.txt`: generation sampled BEFORE log
take, idle skip requires complete/empty taken ranges, non-quiescent generation mode never
falls through to the log-only path. Existing mode stays opt-in/OFF; BDA epochs/batch/upload
ordering untouched. Regression deliberately drops a log entry (not a reproduced live
tracker omission), reads actual native bytes, checks late acknowledged generation,
neighbors/subpage/full-page publication/no drain. No scripts/config/agents/commits/pushes.

Worker unchanged-engine failing fixture `oc20261007-015820-tests` reproduced stale native
bytes; after fix focused `oc20261007-015916-tests` **1/1,0.41s**, full
`oc20261007-015937-tests` **56/56,30.61s**, standard
`oc20261007-020018-build` installs **Dev124**. Scoped diff-check passes; preflight clear.

Sessions under `_Build/autolab/`, worker jobs `oc20261007-020054-fight` / `oc20261007-020854-fight`:
**`d124-bdagen-publication-framedump-a-20261007-020116`** generation ON:
420.38s,first95.52,observed324.86,4957frames/**15.31FPS**,opening45**16.72**,
median/min17/1,RSS11129.4/VRAM12763MB. **`d124-publication-control-framedump-b-20261007-020916`**
generation OFF, same source/default dirty log ON:420.47s,first95.48,observed324.99,
6690frames/**20.68FPS**,opening45**17.08**,median/min19/14,RSS11183.3/VRAM12828MB.
Both timeout/exit0,2560x1440/Mailbox,validation/red-zone/breadcrumbs/2s engine frames,
NaN trace/dumps/Tracy OFF,**fg=False**,no loss/driver/validation errors,stderr empty.
Final nominal30s waits A/B cp**427.74/84.92**,other**479.97/94.75**,
untagged**18.61/7.07**,write**1.11/0.79**ms/s. BDA fast fractions76.67/97.33%,
generation skips vs dirty-log operations; not equivalent costs.

**A visual failure:** engine160 R1 2:00,1701:48,1751:44 have massive stretched skin/
black triangles with live HUD. Full-walk-on-transition generation mode did NOT fix all
corruption. B120standing/160ground/175submission/185ground have visible fighters in
reviewed samples; upper background dark/speckling remain, no clearance/manual transient
check. Different late standing/ground scenarios; B is a switch control not old-code
patch-causation proof. Prior Dev122 generation-OFF stretching still valid. **Retain small
regression-proven dormant-path correction; reject generation activation/visual/FPS
acceptance**, no default change and no correct20/25/30 or media goal.
Next: GPU-image->buffer/backing publication fixture with existing cached buffer/native
BDA consumer; deferred WriteBacking, ownership/completion/BDA-epoch proof, not unchanged
soaks. Readback independent lead cp84.92/other94.75ms/s. Fresh progress triage addressed;
actual full suite56, advisory mistaken55/19.58 not facts.
Report `_Build/agent-reports/dev124-bda-generation-milestone-20261007.md`.

### UFC Dev 123 — owned graphics-stage checkpoint provenance; 55/55; two diagnostic fights (2026-10-07, 01:54)

One bounded engine diagnostic/publication correction, **not a visual fix/FPS gain**.
Changed `renderer/{gpuCheckpointHistory.h,gpuCrashDiagnostics.{cpp,h},context.cpp,
render.h,renderDraw.cpp}` and `tests/ResourceTrackingTests.cpp`. Draw execution phases
copy actual compiled guest hashes/native specialization IDs/stage/wave sizes after
resource restarts; initial/non-draw operations remain empty, no inherited shader state.
Common emission covers indexed/auto/direct/GPU-indirect/mesh/tessellation. Mutex-owned
history snapshots fix report/annotation races and reject expired tokens. Existing diagnostic
flags gate work; no new GPU command/wait/output/default change; prior uncommitted work preserved.

Worker focused `oc20261007-013048-tests`: resource_tracking **1/1,0.03 s**;
full `oc20261007-013201-tests`: **55/55,33.20 s**; standard
`oc20261007-013248-build` installs **Dev123**. Regression proves owned four-stage/phase
metadata, empty resets, compute isolation, expiration/wrap and concurrent coherent reports.
Scoped diff-check passes. No automation/supervisor/config edits, commits or pushes.

Serialized jobs `oc20261007-013325-fight` / `oc20261007-014422-fight`, sessions under
`_Build/autolab/`: **`d123-graphics-checkpoint-nantrace-framedump-20261007-013347`**
(600.12 s,first142.74,observed457.38,8921 frames/**19.58 FPS**,opening45**15.01**,
median/min19/5,RSS11816.5/VRAM12934 MB) and
**`d123-graphics-checkpoint-framedump-repeat-20261007-014443`**
(419.93 s,first95.50,observed324.43,6233 frames/**19.27 FPS**,opening45**14.54**,
median/min16/13,RSS11183.7/VRAM12807 MB). Both timeout/exit0,2560x1440/Mailbox,
validation/red-zone/breadcrumbs/engine PNGs,no Tracy,no loss/driver/validation errors,
live watcher,**fg=False**. A NaN trace/shader dump ON; B both OFF; not paired/FPS acceptance.
Last nominal30-s waits ms/s A/B cp_read_fault**22.41/75.83**,other**38.57/88.23**,
untagged**16.80/12.18**,write**1.75/1.79**; round/workload differ.

Live A19/B14 graphics checkpoint samples have nonzero per-stage hashes/program IDs,
including PS-absent draws (no leaked PS). A seq10207232 VS`e6c549c4d345d6b0`
program627/PS`10e819a1a4f7f467` program626 match saved original/native shaders.
Legacy scalar `shader_hash` remains compute-only; graphics are in `graphics[i]`.
No new TDR, so no actual culprit verdict. A22 completed trace-site reports repeat
pre-fight VS7863.../CS8dbc...; no new selected prior positive VS-group inputs.

Reviewed A engine50/120/180/240/244 and B50/120/160/185 show actual standing/ground
R1/R2,HUD/fighters/arena; speckling remains, B120 upper background dark. No large
failure in these selected samples, **not artifact clearance**; manual transient check
missing. Prior Dev122 trace-OFF stretching remains positive evidence. **Retain bounded
diagnostic only**, not stability/performance/visual acceptance; no media/correct20/25/30 goal.
Next: failing GPU-image->buffer/CPU dirty-log/BDA-epoch upload fixture for the stretched
geometry lead (dirty log ON), then evidenced correction; independent readback target
cp75.83/other88.23ms/s with pending-write proof. Do not repeat unchanged soaks, change
input schedules or treat post-KO Dev12213.77 summary as a control.
Report `_Build/agent-reports/dev123-graphics-checkpoint-milestone-20261007.md`.

### UFC Dev 122 — failed capture reconciled; trace-OFF control captures mesh stretching (2026-10-07, 01:25)

One necessary bounded diagnostic/control milestone; **no engine/default/save changes or new
build**. Preserve all prior uncommitted work on dev. Consumed actual Dev122 input capture,
not stale Dev121 acceptance: `d122-nantrace-input-framedump-20261006-183410`, 587.29 s,
first fight151.50, detected435.79 s,3057 frames/**7.03 FPS**, median/min15/0,RSS11142.6 MB,
VRAM12881 MB. Engine37/100 renders fight,140 black with HUD,196 large black geometry.
Five nvlddmkm153, timeout/InstructionPointerUnknown; DrawIndex seq20469630/31 submit685153,
hashes0, PS/ES `1152c5ce00/11422cf000` and `1142767800/1141dfa000`. Completed owned
inputs for VS `2c4e5b0c45d615eb/c44c878e59f9e5ff/343814752fbf40a9` all wave64,
position raw all7fffffff; descriptor snapshots are NOT GPU buffer content/causal proof.

Preflight clear; worker `oc20261007-011229-fight` exit0,
`_Build/autolab/d122-tdr-control-framedump-20261007-011250`: unchanged Dev122,600.89 s,
2560x1440/Mailbox,validation/red-zone/breadcrumbs,engine frames,no shader dump/Tracy,
**NaN tracing OFF**, a-p ON banner/runtime dirty log ON. No loss/driver events.
**Visual failure still present:** engine120 R1 2:26 shows long stretched fighter mesh during
ground-and-pound;126/128 recover. Standing40/submission100/finish130/replay140/results196/262
inspected; speckling remains. Trace not required for this artifact; not a verdict about the
prior spreading-black/TDR cause. No screenshot-only clearance/manual transient check.

Raw summary first_fight93.57,detected507.32 s,6968 frames/**13.77 FPS**,median/min8/6,
RSS11458.9 MB/VRAM13097 MB. **Summary includes replay/results after KO**, not507 s combat.
Conservative actual-combat samples94.98->301.23:4523 frames/206.25 s/**21.93 FPS**;
interpolated opening45 **15.76 FPS**. Foreground **fg=False**, early KO/standing-ground/cache/
instrumentation differ: no performance-gain/comparable acceptance claim. Late combat wait
delta ms/s(cp/other/untagged/write):**81.79/93.74/5.30/0.70**. Last results interval instead
27.58/16.79/0/0 plus cpu-read17.04,srt-read12.15; not a fight-performance reference.

Reused actual full worker tests `oc20261006-183222-tests` **55/55,34.53 s** (resource_tracking
0.02,compute12.61,scheduler0.31); prior standard build `oc20261006-183309-build` installed122.
No focused/full tests newly run this cycle, because code unchanged. Changed HANDOFF/LOOP-LOG
and `_Build/agent-reports/dev122-tdr-control-milestone-20261007.md` only. **Retain candidate for
diagnosis, NOT visual/stability acceptance; no correct20/25/30-FPS or media milestone**.
Next engine correction: compiled graphics shader identities in breadcrumb records at actual
draw emission, including phase resets/direct/indirect/mesh; currently only compute annotation
exists (`renderCompute.cpp489`). Own/publish metadata safely, test provenance/expiration,
then shader-dumped targeted acceptance through worker. Do not infer hashes from post-fault
guest memory or repeat another unchanged soak. Prior input buffers retain producer lead.

### UFC Dev 121 — NaN recording-event tokens + exact output words; 55/55; two diagnostic fights (2026-10-06, 18:29)

One concrete engine diagnostic milestone, **not a blackout fix/FPS optimization**.
Preserved existing uncommitted source/defaults. Changed `NanTrace.{cpp,h}`,
`nanTraceSnapshot.h`, `render.h`, `pipeline/{descriptors,pipelineCache}.cpp`,
`ShaderRecompiler.cpp`, `ir/ShaderIR.h`, `ir/passes/BindingLayout.{cpp,h}`,
`spirv/{SpirvEmitter,spirvEmitterModule,spirvEmitterMemory}.cpp`, and
`{ResourceTrackingTests,ShaderRecompilerComputeTests}.cpp`. CAS winner records a
per-binding-commit push token and raw float4; completed snapshot remains publication proof.
Trace-only word31 reservation uses existing guest-data storage fallback, preserving mesh/
viewport words. Bounded host history rejects expired/recycled IDs. No extra GPU submit/wait,
clamps or guest-output changes; tracer remains **OFF unless KYTY_UFC_NAN_TRACE=1**.

Worker focused `oc20261006-180149-tests` **command_scheduler_timeline 1/1**, 0.46 s;
`oc20261006-180252-tests` **resource_tracking 1/1**, 0.03 s; full
`oc20261006-180312-tests` **55/55, 32.49 s**; `oc20261006-180353-build` installs **Dev121**.
Fixed initial test-only qualification/link-coupling errors; no remaining failure.
Regression proves first-winner payload, later-event non-overwrite, atomic reads, unchanged
64-layer GPU exports, incomplete-copy rejection, token expiration, and push fallback boundaries.
Scoped tracked-file diff check passes. No harness/supervisor/config changes or commits/pushes.

Serialized worker fights, 2560x1440/Mailbox launch, foreground/validation/red-zone ON,
NaN trace + engine frames, no Tracy, Makhachev/Della Maddalena:

| Session under `_Build/autolab/` | Duration / first fight / observed fight s | Frames / fight FPS / opening45 FPS | Median/min | RSS / device VRAM MB |
|---|---|---|---|---|
| `d121-nantrace-event-framedump-20261006-180505` (shader dump) | 601.06 / 406.92 / 194.14 | 3366 / **17.51** / **16.17** | 16/9 | 11637.9 / 13540 |
| `d121-nantrace-event-framedump-repeat-20261006-181553` (no dump) | 420.50 / 157.88 / 262.62 | 4827 / **18.48** / **14.44** | 16/8 | 11119.3 / 12721 |

Jobs `oc20261006-180443-fight` / `oc20261006-181533-fight`: exit0/timeout,
live watcher, no GPU loss/driver events, empty stderr; zero-FPS samples outside fighting only.
Nominal last30-s wait deltas ms/s (A/B): cp_read_fault **71.13/68.49**,
other_threads **88.42/88.02**, gpu_untagged **12.80/13.63**, cpu_write_flush **1.48/1.39**.
A150/165/180/220 and B65/90/120/155 show standing/ground fights, skin/cage/mat speckling;
no large stretched/black failure in inspected images, **not visual clearance**. Existing
3840x2160 guest dump source unchanged; manual transient check missing. Cold/loading/scenario
differences mean no matched performance improvement/regression claim. Neither run reaches
the earlier positive late-R2 scenario; Dev119/120 corruption evidence remains valid.

**New evidence:** A18/B20 completed site reports, every token resolves, no expired-history
reports. CS `8dbc785aee6c1b3d` binding19 NaN/Inf share the same indirect dispatch in each
run (A event9928081 frame1497/tick500294/submit462227; B event889099 frame914/tick209352/
submit187468). B VS `7863cceac4a3fa7e` position0 NaN belongs to event238437,
recorded frame729/tick137932/submit123917, DrawIndex phase100/count54/instances1,
raw `7fffffff/7fffffff/0/3f800000`; first hit precedes verified fighting, not a culprit verdict.
Event args are debug-phase fields; graphics `address` is first-instance, **not resource addr**.
Tokens identify binding commits, not multi-draw subdraws or GPU causal order; full resource
contents/primitive identity still absent. **Retain opt-in diagnostic only**, no visual/media/
correct 20/25/30-FPS goal achieved. Next: targeted immutable SGPR/resource-descriptor inputs
and output formats for the prior positive Dev120 VS group (`2c4e5b0c45d615eb`,
`c44c878e59f9e5ff`, `343814752fbf40a9`), not another unchanged soak or first-stamp accusation.
Report: `_Build/agent-reports/dev121-nantrace-event-milestone-20261006.md`.

### UFC Dev 120 — NaN trace completion/read-race correction; 55/55; two diagnostic fights (2026-10-06, 17:53)

One bounded engine diagnostic correction, **not a blackout fix or FPS optimization**.
Preserved all earlier uncommitted work/defaults. Changed `renderContext.{cpp,h}`,
new `renderer/nanTraceSnapshot.h`, `NanTrace.{cpp,h}`, `spirvEmitterMemory.cpp`, and
`ShaderRecompilerComputeTests.cpp`. Host never polls the live shader-written allocation:
one barriered download slot is read only after its timeline tick completes and copied
to owned immutable words. No forced submission/wait. Emitter slot fast-path loads are
now atomic; log frame/tick mean snapshot boundaries, not exact draw/causal GPU order.
Tracer remains **opt-in, OFF absent `KYTY_UFC_NAN_TRACE=1`**.

Worker focused `oc20261006-173052-tests`: command_scheduler_timeline **1/1**, 0.48 s;
full `oc20261006-173205-tests`: **55/55, 37.06 s**; build
`oc20261006-173307-build`: **UFC Dev 120** installed. Regression covers unsubmitted and
externally gated incomplete copies, refusal to overwrite pending snapshots, same-submit
later writes, immutable reuse, SPIR-V atomic reads, 64-layer contending NaN/Inf exports
and unchanged output. `git diff --check` passes for touched tracked engine/test files.

Serialized worker gameplay, 2560x1440/Mailbox launch, validation/red-zone/breadcrumbs ON,
NaN trace + engine dumps, no Tracy, same fighters (Makhachev / Della Maddalena):

| Session under `_Build/autolab/` | Duration / fight start / observed fight | Frames / fight FPS / opening 45 s | Median/min | RSS / device VRAM MB |
|---|---|---|---|---|
| `d120-nantrace-snapshot-framedump-20261006-173431` (shader dump) | 600.54 / 226.12 / 374.42 s | 3639 / **9.77** / **16.32** | 15/1 | 11429.7 / 13178 |
| `d120-nantrace-snapshot-framedump-repeat-20261006-174519` (no shader dump) | 421.02 / 163.15 / 257.87 s | 4431 / **17.26** / **13.48** | 15/7 | 11204.1 / 12895 |

Both jobs exit 0 at monitor timeout; **no device loss/driver events**, live watcher.
Last nominal ~30-s wait deltas, ms/s (A/B): cp_read_fault **658.87/79.03**,
other_threads **610.73/89.76**, gpu_untagged **13.85/8.06**, cpu_write_flush **0.72/1.31**.
A has 51 completed site reports, B 18. A captures genuine large black/stretched geometry
in engine 145/170/207 with live HUD; engine 75/130 also render ground fighting. B engine
90 (standing) and 155 (ground) render without that large failure in inspected images;
skin/cage speckling persists. **No screenshot-only visual clearance**; manual transient
check still missing. Launch summary says 2560x1440; both engine dumps report a 3840x2160
source (existing guest output behavior), not a quality/resolution change this cycle.

**Retain diagnostic correction only. Visual/gameplay performance acceptance NOT passed.**
Cold shader/pipeline work and corruption differ: A logged ~88.4 s cumulative pipeline
creation before fighting; this is not a paired performance control or proof of patch
regression. Previous Dev 119 already has positive corruption; no culprit established.
Next: add exact draw/dispatch/resource provenance to completed records, then inspect the
producer/consumer inputs for the VS position NaN/Inf group (A snapshot 1177/315494 and
1646/467322), alongside the existing late-R2 five-site lead. Do not accuse the first
stamp, clamp shaders or collect another unchanged soak just to repeat the verdict.
Media playback/repeated correct 25/30 FPS remain unverified. Report:
`_Build/agent-reports/dev120-nantrace-snapshot-milestone-20261006.md`.

### UFC Dev 119 — resumed NaN diagnostic: watcher survives; late R2 black shapes correlate with five new trace sites (2026-10-06, 17:25)

Active authority is `OVERNIGHT-PLAN-20261006.md`'s explicit Dev 119 restart, not the
historical STOP review below. One bounded diagnostic/verification milestone; **no
engine/default/build changes**. Our worker tests `oc20261006-171124-tests` pass
**55/55, 35.77 s** (compute regression 14.19 s). Consumed the first tracer's evidence:
374.94-s early exit, dead watcher, but actual final screenshot shows R1 fighting.
No valid automated fight metric from that first run.

Before another launch, found an already-running worker follow-up; did **not** duplicate
or interrupt it. `oc20261006-171138-fight` exit 0, session
`_Build/autolab/d119-nantrace-framedump-followup-20261006-171221`, **600.14 s**, no Tracy,
2560x1440/Mailbox launch, validation/red-zone/breadcrumbs ON. Watcher survives to final
snapshot (no WinError 5 traceback). First fight **136.35 s**, fighting observed **463.79 s**,
**8578 frames / 18.54 FPS**, first-45-s interpolated frame throughput **14.06 FPS**;
fight median/min **18/4**, RSS **11703.6 MB**, device VRAM **13379 MB**, no device loss/events.
Last ~30-s wait deltas: cp_read_fault **30.49 ms/s**, other_threads **40.48**,
gpu_untagged **11.86**, cpu_write_flush **1.77**. Diagnostic/cache/scenario differences
prevent a clean performance comparison.

**Real corruption captured:** engine 240–244 intact standing R2; 245 localized black
shapes, 246–247 expanding silhouettes over fighters/cage, HUD still updating. Desktop
shots 258/260 corroborate (~593–598 s), not a compositor-only failure. Console between
dumps 242/243 logs new NaNs: CS `e17349e0d437757b` binding12, CS `3c6058d66788f544`
binding2; PS `654607b31fe5f6d6`, `487864de9705a169`, `811633d84dc57d64` MRT0. All five
guest/SPIR-V dumps exist in the first tracer session. Polling guest frame **5243** is
not the native title/metrics counter; stamps are not exact GPU-event/causal order.

**Next:** correct trace synchronization before accusing a shader: current host Poll reads
the live GPU-written buffer with only Invalidate, and emitter plain slot loads race with
atomics. Use properly barriered, completed snapshots plus a focused producer/consumer
regression, then add draw/dispatch/resource identity. No NaN clamps/skipped work. Existing
visual positive means another unchanged soak is unnecessary. Retain current source;
**visual acceptance failed**, no media/25/30-FPS claim, manual transient check still missing.
Changed files this cycle: HANDOFF/LOOP-LOG and report only. Full evidence:
`_Build/agent-reports/dev119-nantrace-visual-correlation-20261006.md`.

### STOPPED BY USER — loop ended at Dev 116, 55/55; later manual Dev 117 observed (2026-10-06)

The user explicitly stopped the loop and asked for an accomplishments review. STOP is present;
queue empty; no project supervisor/agent/worker/emulator/build/test processes were running at
inspection. Stale state was corrected to `stopped by user`; no automatic restart is authorized.
FIRST read `../LOOP-STOP-REVIEW-20261006.md` for reconciled actual build/test/results. Older
experiment labels below (Dev 117/118 etc.) are not proof of the installed build identity.

At initial stop inspection, binary label and counter identified **Dev 116**. The loop's final
build `oc20261006-152941-build` completed; its final full tests `oc20261006-153010-tests`
are **55/55 in 34.11 s**. The last
cycle (92, OpenRouter DeepSeek) added compute-preparation/map/extract-plan profiler zones,
then changed EquivalentValue's visited-pair search from vector to hash set in `ir/Program.cpp`.
Its 293.35 s game attempt exited early with no logged GPU loss or detected fight; no completed
acceptance or causal FPS gain is established. Preserve existing work; no rollback was performed.

Fresh profile reconciliation localizes the multi-second stall to **Shader::ExtractPlan**, not
the shader-map mutex: Dev 115 max 4.878 s / total 7.031 s (87 calls), Dev 116 max 4.739 s /
total 6.863 s (87 calls). The stall remains. Dev 115's 24.73-FPS detected interval covers only
57.25 s and is profiled, so it is not >=120-s/repeated 25-FPS acceptance. Engine frame dumps
are a useful completed addition; genuine black-HUD/mesh corruption is still recorded. Opening
imagery/arena/gameplay render, but actual movie playback/smoothness remains unverified.

During the review a new worker was opened through Explorer at 15:48, and a manual launcher/
game at 15:58 identify **Dev 117**. They are separate activity; no OpenCode supervisor/agent
has resumed, STOP remains present. Their processes/source were left alone. This stopped-loop
review does not certify or attribute the later Dev 117 changes.

### UFC Dev 114 binary — compute-dispatch attribution: `CpOpDispatchDirect` is program-lookup/SRT-prep dominated with rare multi-second `GetComputeProgram` stalls, not descriptor re-setup (2026-10-06, 15:20)

One bounded diagnostic (overnight plan item 2) plus one **behavior-neutral** engine change,
answering Dev 116's open compute-dispatch question. Added Tracy sub-zones to
`RenderExecutor::DispatchDirect`/`DispatchIndirect` (`src/graphics/host_gpu/renderer/renderCompute.cpp`):
`CS::GetProgram`, `CS::FillChecks`, `CS::PrepareBindings`, `CS::PrepareBda`, `CS::Rebind`,
`CS::ObtainArgs`, `CS::PostBarrier`. No semantic change; inert without the profiler. Build
`dev117-cs-dispatch-zones` → installed **UFC Dev 114**; full CTest **55/55**
(`oc20261006-150827-tests`). Capture `d117-csdispatch-tracy-framedump` (job `oc20261006-150928-fight`,
exit 0, 420.04 s, 2560x1440/Mailbox, validation/red-zone ON, a–p ON, `framedump`), session
`_Build/autolab/d117-csdispatch-tracy-framedump-20261006-150951`, Tracy 29.07 s + 60 s.

**Attribution (60 s Tracy window):** `CpOpDispatchDirect` 10.694 s / 252,304 calls (mean 42.4 µs,
**max 4.691 s**); `CpOpDispatchIndirect` 3.962 s / 20,036 (mean 197.7 µs, **max 2.110 s**).
`CS::GetProgram` = 6.096 s direct + 2.994 s indirect = **9.09 s (62 % of dispatch time)**;
`CS::PrepareBindings` 1.248 s, `CS::Rebind` 1.379 s, `CS::FillChecks` 0.153 s,
`CS::PostBarrier` 0.103 s, `CS::ObtainArgs` 0.002 s; no `CS::PrepareBda` (no DMA dispatches).

**Correction to Dev 116:** the mean is dominated by a few multi-second `GetProgram` stalls (direct
max ≈ whole `CpOpDispatchDirect` max). Excluding the single direct max, typical dispatch is
**≈23.8 µs** (GetProgram ≈5.6 µs). The stall is **not** translation (536 calls/1.85 s/max 128 ms),
SRT materialization (compute 272 k calls/1.58 s/max 0.15 ms), pipeline creation (0.067 s) or key
finding (0.05 s) — it lies in un-zoned `PrepareProgram`/`ShaderGetMappedData`
(global `g_shader_map_mutex` + `unordered_map`), the lead for the next cycle. Descriptor/pipeline
re-setup is **not** the dominant compute cost.

**Correctness:** `gpu_device_lost=False`, no driver events, breadcrumbs clean; engine frames render
content (means 35/69/84; only first + load-transition frames black, as Dev 116); only the benign
`SPIR-V validation cache: 799`. `fight_detected=False` is the `framedump` FightWatch false-negative;
desktop shots black → **manual live check still required**. **Observation (not attributed):** this
420 s run accumulated far larger CPU waits than Dev 116 (`cp_read_fault` 143 s, `other_threads`
153 s cumulative, sharp after ~240 s) with clean breadcrumbs/no device loss. No 25/30-FPS or media
claim. Report: `_Build/agent-reports/dev114-csdispatch-attribution-20261006.md`.

**Next:** zone `PrepareProgram`/`ShaderGetMappedData` to localize the multi-second stall; if it is
`g_shader_map_mutex` contention, shard or make the shader map read-mostly.

### UFC Dev 116 — launch-window capture: gameplay renders; load-into-gameplay "blackout" is a ~24 s 1–7 FPS window (2026-10-06, 14:30)

One bounded diagnostic (overnight plan item 1/2), **no source/default change**, on the unchanged
Dev 113 install. The prior launch capture started Tracy at 45 s and used desktop `PrintWindow`
shots (always black for the Vulkan client). This run captures from launch with **engine
presented-frame dumps**. Session `d095-load-intro-diag-framedump-20261006-141940`, job
`oc20261006-141918-fight` exit 0, 241.56 s, 2560x1440/Mailbox, validation/red-zone ON, a–p ON,
`framedump`+`shader_dump`; Tracy 29.01 s + 50 s; 104 engine frames. No errors, no device loss, no
driver events, `SPIR-V validation cache: 799`.

**Engine frames render every phase correctly:** EA Sports intro montage (~10 s), UFC 5 logo
(~24 s), character select with HUD (~48 s), arena walkout lit (~81 s), ground-and-pound fight with
HUD (~144 s, ~167 s). Fully black frames exist only at the first presented frame (~2 s) and one
load-transition frame (~80 s). **No video-decoder activity was logged** — the automated press
schedule does not exercise the movie path.

**FPS:** 54–61 (boot/menus 2–29 s) → 9 (31 s, first load) → 19–28 (character-select/intro 33–53 s)
→ **VRAM +3 GB at 54.8 s → 1–7 FPS for ~24 s (56–80 s, the load-into-gameplay black/dark
window)** → 14–17 (82–133 s) → 14–30 fight (sustained 24–30 around 152–190 s). Median 19, min 1,
max RSS 11,183 MB, VRAM ~12.3 GB.

**Load-window zones (Tracy 29–79 s):** `DrawIndex` 28.9%, `PrepareDrawRenderState` 15.2%,
`ExecutePreparedDraw` 14.3%, `RefreshShaders` 13.3%, `CpOpDispatchDirect` 11.7% (185,801
dispatches, mean **50 µs**), `MasterSemaphore::Wait` 11.1%, `PrepareGraphicsBindings` 9.4%,
`SrtRefreshFlatBuffer` 8.5%, BDA sync 7.1%, `BufferCache::ReadMemory` 6.4% (6,590 calls, mean
770 µs), image create+delete ~7.3%. Cumulative waits `cp_read_fault=6997 ms`,
`other_threads=13217 ms`; BDA fast path 96.9%.

**Conclusion:** the user's blackout reproduces as the **~24 s near-zero-FPS load/transition
window (56–80 s)**, not a persistent rendering failure; no single feature is implicated. Desktop
shots remain black (known artifact); engine frames are the only reliable visual evidence, so a
**manual live check is still required**. No 25/30-FPS or media claim. Report:
`_Build/agent-reports/dev116-load-intro-diag-20261006.md`.

**Next:** attribute the 56–80 s load window — whether the ~50 µs `CpOpDispatchDirect` cost
(per-dispatch pipeline/descriptor re-setup) or CPU read-fault drains can be reduced with a
bounded, shadow-verified change; else continue the fight-window P4 work. Confirm movie playback
only on the manual path.

### UFC Dev 115 — clean BDA dirty-log (c) ON/OFF A/B: 97–98% walk cut, early-window +11–16% FPS (2026-10-06, 14:20)

One acceptance-check milestone on the unchanged Dev 113 install, **no source/default change**. The
Dev 114 next-step #2: four 300 s `framedump` runs (2560x1440/Mailbox, ShaderValidation/red-zone ON,
a,b,d–p ON, same press schedule) alternating c ON (default) and c OFF (`*bis-c*`). Sessions:
`d115-cab-a-con-framedump-...135116`, `d115-cab-b-bis-c-framedump-...135646`,
`d115-cab-b2-bis-c-framedump-...140252`, `d115-cab-a2r-con-framedump-...140912`
(A2's first attempt `...140825` was an exit-321/duration-0 guest-memory launch failure — retried,
not data). Report: `_Build/agent-reports/dev115-ca-dirtylog-ab-20261006.md`.

**Mechanism:** `bda sync` fast-path coverage **97.0%/97.8%** with c ON (walk 13506/13815) vs
**0%** with `bis-c` (walk 413431/315177); `verify_missed=0` in both c-ON runs. `gpu_device_lost=False`,
no driver events, all four.

**FPS (`metrics.csv` frame deltas, cON mean vs cOFF mean):** 60–105 s 10.03 vs 8.65 (**+15.9%**);
110–145 s 15.62 vs 14.07 (**+11.0%**); 150–195 s 16.27 vs 19.77 (−17.7%); 200–245 s 21.61 vs 26.06
(−17.1%); 250–300 s 24.91 vs 17.48 (+42.5%); whole 60–300 s 17.98 vs 17.60 (**+2.2%**). The
early-fight windows favor c ON in **both alternating pairs**; later windows are scene/workload
divergent, so **no robust whole-run gain is claimed**. `fight_detected=False` is the known
`framedump` FightWatch screenshot false-negative. RSS 11,063/11,001/11,177/11,169 MB.

**Correctness:** a late-fight dark engine-frame phase (dark-fraction ~0.40–0.45) appears with c
**both OFF and ON** (B 111–131, B2 125–132, A2 intermittent) → **not c-specific**, further
weakening the Dev 108 blackout association. **User manual mesh-explosion / ground-and-pound check
remains outstanding.**

**Decision:** no engine/default/script change; keep the run-level `bis-c` quarantine (harness rule:
manual visual check outstanding). No 25/30-FPS or media claim.

**Next:** (1) if the manual check clears, drop `bis-c` from acceptance runs and re-baseline (the
fast path is the only evidenced ~7% command-thread saving available); (2) else attack the next
measured P4 zone (`RefreshShaders`/`PrepareGraphicsBindings`) with shadow-verified reuse; (3)
media/intro early-window attribution — shader-build totals in these runs (`translate 2408 / compile
723 / validate 3 / module 28 ms`, 868 pipelines) show first-use translation is a ~2.4 s one-time
cost, not a sustained-FPS lever.

### UFC Dev 114 — BDA dirty-log fast-path verify: no tracker-visible missed upload; late-fight black geometry (not attributed) (2026-10-06, 13:50)

Bounded P2 diagnostic on the unchanged Dev 113 install, no source/default change. The Dev 107
fighting profile attributes `Draw::PrepareBda`/`SynchronizeBuffersInRanges` ~6.6% of all traced CPU
(~33% of the command thread, 119 µs/call) with dirty log (c) OFF. Two worker-owned 420 s runs
(`framedump` engine frames, d–p ON, a/b OFF to isolate the dirty-log path from blackout suspects):
`d114-dlverify-bis-ab-…132944` (c ON + `BDA_DIRTY_VERIFY=1`) and `d114-dlon-bis-ab-…133851` (c ON,
verify OFF). Both exit 0, no device loss/driver events.

**Result:** run 1 `bda sync fast=817642 walk=8629 verify_missed=0 verify_raced=171759`; run 2
`fast=653022 walk=11550 verify_missed=0 verify_raced=0`. The `BDA dirty log MISSED` line fired **0**
times. The fast path handled **98.3–99.0%** of BDA syncs (vs 75.5–81.9% for Dev 112 `bdagen`) and
`VerifyBdaDirtyLog` found **no** CPU-dirty page absent from the log across 1.47M fast-path syncs.
The historical dirty-log "missed upload" is therefore **not reproduced as a tracker-visible miss** on
Dev 113; do not repeat the same oracle. No FPS claim (420 s duration, a/b state and the verify walk
differ from baselines; Windows shots remain black — engine frames render the arena). Sampled median
14/20, max RSS 11,050.5/11,279.1 MB.

**Incidental (not attributed):** both 420 s runs show a transient giant black mesh geometry / all-black
engine frame near the end of the fight (run 1 frames 180–185 dark 0.51–0.73; run 2 frame 181 fully
black); frame 160/168 clean. Three 300 s c-OFF `framedump` baselines show none above 0.4, but they end
~300 s while the artifact appears ~405–420 s (duration confound), and one historical 420 s c-OFF run
also showed it. Open correctness item, not dirty-log-specific.

**Decision:** keep c as configured (default ON, `bis-c` quarantine in performance runs unchanged); no
promotion — user manual visual check still outstanding. No 25/30-FPS or media claim. Report:
`_Build/agent-reports/dev114-dirtylog-verify-20261006.md`.

**Next:** (1) 420 s c-OFF `framedump` control at matched duration/a-b state to test whether the
late-fight black geometry is c-specific; (2) if exonerated, repeated 300 s c ON vs c OFF same-settings
A/B to measure the 99% walk reduction (still gated on the user's manual check); (3) independent
media/intro early-window Tracy attribution.

### UFC Dev 113 — multi-slot SRT memo: shadow-verified, but per-draw SRT is irreducible (rejected lever) (2026-10-06, 13:35)

Bounded P4 experiment against the largest measured command-thread cost. The d094 early capture
(45–105 s, 60.2 s, 694 frames) attributes `MaterializeResources` ≈ 13.7 s / 60 s (SRT
`RefreshFlatBuffer` 10.6 s) — the biggest single zone — while the existing `KYTY_UFC_SRT_MEMO`
reused only **2.1%** of calls (`reused=268187 inputs=12459340`, Dev 113 control). Hypothesis: the
memo's single slot is clobbered by interleaved keys, so a bounded multi-slot per-source memo would
reuse more. Implemented `KYTY_UFC_SRT_MEMO_SLOTS` (default **1** = original single slot; 8/64
opt-in) with the same `ReplaySrtReads` invalidation proof and a shadow mode. Files:
`src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`, `AutoFight.ps1` (labels
`*srtmemoslots*`=8, `*srtmemoslots64*`=64, `*srtmemoslots-shadow*`=shadow verify).

Worker build **oc20261006-125918-build = UFC Dev 113** installed; focused
**resource_materialization/resource_tracking/shader_cfg/scalar_provenance/shader_recompiler_compute
5/5**; full **tests 55/55 (34.24 s)**.

Three 300 s runs, Dev 113, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c (dirty log) OFF,
`framedump`; all `gpu_device_lost=False`, no driver events, no post-intro dark engine frame:

| run | slots/mode | reused | inputs | reads | shadow_mismatch | hit rate |
|---|---|---|---|---|---|---|
| `d095-srtmemodefault-bis-c-framedump-20261006-131525` | 1 / 1 | 268192 | 12694369 | 1 | 0 | **2.07%** |
| `d095-srtmemoslots-shadow-bis-c-framedump-20261006-130051` | 8 / shadow | 262361 | 11798930 | 6547 | **0** | **2.18%** |
| `d095-srtmemoslots64-shadow-bis-c-framedump-20261006-130726` | 64 / 1 | 400835 | 11877275 | 79494 | 0 | **3.24%** |

**Result — the lever fails.** 1→8→64 slots moves hits only 2.07%→2.18%→3.24%; even a 64-slot
(memory-costly) cache adds ~1.2% of calls. `reads` (a slot matched inputs but a logged guest read
replayed differently) climbs 1→6547→79494: the SRT guest data itself changes between frames, so the
materialization is genuinely per-draw work, not redundant. **`shadow_mismatch=0` on every reuse
proves the multi-slot reuse was exact** — the mechanism is correct, just not useful here. FPS is not
comparable across these runs (phases differ: 60–300 s 13.87 / 12.31 / 17.30 FPS), and the ~1% call
delta is far below run-to-run noise, so no FPS claim.

**Decision:** keep `KYTY_UFC_SRT_MEMO_SLOTS` **opt-in / default 1** (default counters identical to
the Dev 113 control, so the baseline is unchanged); **do not promote** and do not invest further in
cross-draw SRT reuse. No default behavior changed; red-zone, validation, wave32 mask branches, BDA
batch and runtime viewport preserved. No 25/30-FPS or media claim. The user manual
ground-and-pound / mesh-explosion check remains outstanding and non-blocking.

**Next hypothesis:** per-draw SRT is irreducible, so stop caching it. The other large command-thread
cost in the same capture is BDA sync (`Draw::PrepareBda` ≈ 7.3 s / 60 s, 51 µs/call) — attack the
**walk** cost (dirty-log P2 missed-upload root cause, or a dirty-buffer index), not the generation
skip; or run the plan's item-2 early-window attribution on Dev 113 to separate media/intro work.

### UFC Dev 112 unchanged — clean `bdagen` A/B acceptance: skip works, FPS gain not robust, no blackout (2026-10-06, 12:50)

Ran the Dev 112 next step as a four-run acceptance check (no source change): two clean
`KYTY_UFC_BDA_SYNC_GENERATION` runs (label `bdagen`, **verify OFF** so the skip fires) plus two
matched `bdagen`-OFF controls, alternating order. Dev 112, 2560x1440/Mailbox, ShaderValidation/
red-zone ON, features a/b,d–p ON, c (BDA dirty log) OFF, `framedump`, 300 s, fast mode. Sessions:
A1 `d113-bdagen-clean-a-bis-c-framedump-r2-20261006-122446`, A2
`d113-bdagen-clean-a2r-bis-c-framedump-20261006-124516`, B1
`d113-control-clean-b-bis-c-framedump-20261006-123025`, B2
`d113-control-clean-b2r-bis-c-framedump-20261006-123818`. Report:
`_Build/agent-reports/dev113-bdagen-clean-ab-20261006.md`. No build/tests this cycle (no code
change); baseline preserved.

**Mechanism confirmed:** A1/A2 `bda sync fast/walk = 414484/134617`, `482246/106672` (75.5%/81.9%
skip); B1/B2 `0/356067`, `0/332932` (all walk). BDA walks −62–70%.

**FPS — no robust whole-run gain (phase-dependent).** A-mean vs B-mean: 60–105 s 10.74 vs 8.13
(+32%); 110–145 s 16.34 vs 12.80 (+28%); 150–195 s 16.79 vs 16.53 (+2%); 200–245 s 22.87 vs 26.46
(−14%); 250–300 s 20.34 vs 21.74 (−6%); whole 60–300 s **17.68 vs 17.56 (+0.7%)**. `bdagen` leads
by +270–290 frames at 160–200 s, then the last ~60 s erases it (100–200 s +13.3%; 100–300 s
−1.7%). Dev 108's single-pair +19.8% matched-window is **not** reproduced as a stable effect.

**Correctness:** both `bdagen` runs show only the same startup/loading near-black frames (engine
indices 0–31) as the controls; **no sustained post-intro blackout** (weakens the Dev 108
association; now 3 clean vs 2 black historically). The required **user manual ground-and-pound /
mesh-explosion check remains outstanding and non-blocking**.

**One pre-existing GPU TDR** (A2 first attempt, 30.4 s: `gpu_loss_class=timeout`, nvlddmkm:153,
breadcrumbs `DrawIndex` ×7, fault `WriteInvalid` addr 0) — P1 baseline fault, retried, not
attributed to `bdagen`. Two guest-address-reserve launch failures (exit 321/duration 0) were
retried, not data.

**Decision:** keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in / default OFF; do not promote.** No FPS
or 25/30-FPS claim. Last-30 s blocking sites: `cp_read_fault` 610–1660 ms, `other_threads`
1040–2044 ms, `gpu_untagged` 24–441 ms.

**Next:** pivot to the intro/media window — 60–105 s is the slowest segment (8–11 FPS) and the
mandate's target — with an early `--tracy-start-seconds` attribution on Dev 112 (compute
dispatches, submissions, CPU-GPU waits, first-use translation/pipelines). Alternatively begin the
measured P4 per-draw work (`PrepareGraphicsBindings` 7.4% / `RefreshShaders` 4.8% /
`MaterializeResources`) with a shadow-verified cache and explicit invalidation proof. Note: light
phases already exceed 25 FPS (B2 25–28 FPS at 253–280 s; 200–245 s window 21–26 FPS across runs).

### UFC Dev 112 — post-oracle generation recheck: all hits are races, `verify_missed=0` (2026-10-06, 12:30)

Settled the Dev 110 vs Dev 111 reading of the generation-gated BDA skip. Moved per-hit counting
out of the oracle: `BdaSkipWouldMissUpload` now returns the CPU-dirty span count (`size_t`) and no
longer notes WaitStats; the caller re-reads `CpuDirtyGeneration()` **after** the oracle's long scan
and classifies each hit `raced` (generation bumped) vs `missed` (unchanged). Default behavior
unchanged; all behind `KYTY_UFC_BDA_SYNC_VERIFY=1`. Files:
`renderer/cache/bufferCache.{h,cpp}`. Report:
`_Build/agent-reports/dev112-bdagen-reclass-20261006.md`.

Worker build **oc20261006-121117-build = UFC Dev 112** (installed); full **tests 55/55 (34.78 s)**.

Diagnostic **oc20261006-121239-fight** →
`_Build/autolab/d112-bdagenverify-reclass-bis-c-framedump-20261006-121239`, exit 0, 421.97 s; Dev 112,
2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c (BDA dirty log) OFF, bdagen+verify ON,
framedump; no device loss/driver events, all breadcrumbs started==completed; 187 engine PNGs,
max RSS 11,179.9 MB.

**Finding:** final `bda sync fast=490605 walk=200077 verify_missed=0 verify_raced=19279`. **Every
one of the 19,279 oracle hits raced** (generation bumped during the scan); the new
`BDA gen-skip MISSED` line (emitted only when the generation was unchanged) fired **0** times.
128 `BDA postwalk` reports all had `gen_post > gen_pre`. Dev 110's `verify_missed=11,080` was an
artifact of the oracle's own long scan racing guest writes — the invariant holds. Engine frames:
only 3 near-black samples, all startup/loading; no sustained post-intro blackout.

**Decision:** keep `KYTY_UFC_BDA_SYNC_GENERATION` opt-in/default OFF for now; the diagnostic
exonerates the invariant but the verify path forces the walk and cannot show the gain. No FPS claim
(FightWatch false-negative preserved; `fight_detected=False`); 25/30 FPS and smooth media unmet.

**Next:** two clean `bdagen`-only runs (generation ON, verify OFF so the skip fires) + matched
`bdagen`-OFF controls; compare first-45-s frame counts and engine contact sheets; promote to
default only with a user **manual visual check** for mesh explosions/blackout (record as
outstanding, do not block).

### UFC Dev 111 — BDA post-walk recheck: the walk does publish; residual dirty is a race (2026-10-06, 12:05)

Added an opt-in, side-effect-free post-walk recheck for the Dev 108 generation-gated BDA skip:
`BufferCache::BdaCpuDirtyBufferCount(ranges, log)` re-uses the Dev 110 oracle iteration
(`IsRegionCpuModifiedExisting`, no region creation) and is run immediately **after** the forced
full walk in the verify path, logging `BDA postwalk: gen_pre=<g> gen_post=<g> still_dirty=<n>`
plus `BDA postwalk DIRTY: ...`. All behind `KYTY_UFC_BDA_SYNC_VERIFY=1`; default unchanged.
Files: `renderer/cache/bufferCache.{h,cpp}`. Report:
`_Build/agent-reports/dev111-bda-postwalk-20261006.md`.

Worker build **oc20261006-115533-build = UFC Dev 111** (installed); full
**oc20261006-115609-tests 55/55 (38.66 s)**. Pre-flight clear; heartbeat good; no manual play.

Diagnostic **oc20261006-115802-fight** →
`_Build/autolab/d111-bdagenverify-postwalk-bis-c-framedump-20261006-115823`, exit 0, 421.86 s;
Dev 111, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c (BDA dirty log) OFF,
bdagen+verify ON, framedump; no device loss/driver events; 186 engine PNGs, max RSS 11,168 MB.

**Finding:** of 128 post-walk checks, 54 had `still_dirty=0` and 74 had `still_dirty>0`, and
**all 74 had `gen_post > gen_pre`; none had an unchanged generation.** The forced walk clears the
pages it visits; the residual dirty pages are fresh concurrent guest CPU writes that bump the
generation. The oracle's `verify_missed=8972` vs `verify_raced=2` is better explained by the
oracle's long scan window racing guest writes than by a non-bumping transition, so **Dev 110's
"invariant disproven" reading is not supported** (association, not yet proven). Engine frames show
no sustained post-intro blackout (2/47 sampled near-black, both at startup). No FPS claim;
FightWatch false-negative preserved; 25/30 FPS and smooth media unmet.

**Next:** re-read `CpuDirtyGeneration()` **after** the oracle scan and reclassify a dirty hit as
`raced` when it changed (move per-hit counting out of the oracle); rerun `bdagenverify`. If
`verify_missed` collapses, the generation invariant holds and bdagen becomes a default candidate
(+19.8% matched-window Dev 108 attribution), gated on a user manual visual check. Otherwise
instrument the surviving buffer's dirty-bit writer.

### UFC Dev 110 — BDA generation gate proven unsound by an exact skip oracle (2026-10-06, 11:47)

Added an opt-in, side-effect-free oracle for the Dev 108 generation-gated quiescent BDA skip and
used it to test the gate's invariant. `KYTY_UFC_BDA_SYNC_VERIFY=1` (label `*bdagenverify*`) reruns
the generation recheck, then `BdaSkipWouldMissUpload` over the mapped ranges + `m_buffers`; a hit
counts `verify_missed` (outcome 2), logs `BDA gen-skip MISSED`, and forces the conservative walk.
`BdaSkipWouldMissUpload` uses the new `MemoryTracker::IsRegionCpuModifiedExisting`
(`Iterate<false>`), which never creates a region; `IsRegionCpuModified`'s `Iterate<true>` creates
all-dirty regions and made the first oracle build (Dev 109) self-report — that build is discarded.
Default behavior unchanged; oracle opt-in. Files: `memoryTracker.{h,cpp}`,
`renderer/cache/bufferCache.{h,cpp}`, `AutoFight.ps1`. Report:
`_Build/agent-reports/dev110-bda-gate-oracle-20261006.md`.

Worker build **oc20261006-113214-build = UFC Dev 110** (installed); full **oc20261006-113243-tests
55/55 (35.20 s)**. Pre-flight clear every serial job; heartbeat good; no manual play.

Diagnostic **oc20261006-113512-fight** →
`_Build/autolab/d110-bdagenverify-bis-c-framedump-pooloff-r2-20261006-113533`, exit 0, 421.79 s;
Dev 110, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen+verify ON,
framedump, pool OFF; no device loss/driver events; 186 engine PNGs, max RSS 11,169 MB. (A first
launch, `...-113443`, was the guest-memory-reserve failure — exit 321/duration 0, not data.)

**Finding:** `bda sync fast=423424 walk=194393 verify_missed=11080 verify_raced=1`; the gate
hides uploads on **1.8%** of its calls. 32 `BDA gen-skip MISSED` lines repeat on the same buffers
(`0x12e0000000+0x22f4000`, `0x1294fe0000+0x14000`, 12× each): a range the forced walk just
uploaded reappears next oracle call, so either the walk misses those pages or a CPU-dirty
transition never bumped the generation. The generation invariant is **disproven**. Corroboration:
oracle-corrected engine frames have only 5 near-black frames and a longest post-intro black run of
**3**, versus Dev 108 bdagen's 6–15.

**Decision:** keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in / default OFF; do not promote**. The
oracle fixes correctness but costs the full scan, so it is not a speed path. Retain the oracle +
no-create observer. No FPS claim. **25/30 FPS and smooth media unmet. Next:** instrument per-miss
post-walk clearing / generation-bump to root-cause the hidden transition (suspects:
`InvalidateRegion`/`MarkRegionAsCpuModified` on a not-yet-created region, or a registration racing
the walk's `m_bda_registered_ranges.clear()`); only a bounded fix covering every missed transition
justifies reconsidering bdagen, plus a manual visual check. Otherwise continue intro/media work.

### UFC Dev 108 unchanged — bdagen Tracy attribution + engine-frame blackout finding (2026-10-06, 11:15)

Consumed the planned matched Tracy capture for the retained Dev 108 candidate (`bdagen`).
Job **oc20261006-110920-fight**, exit 0, 300.37 s, session
`_Build/autolab/d108-bdagen-tracy-bis-c-framedump-pooloff-20261006-110941`; Dev 108,
2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen ON, framedump, pool OFF;
`gpu_device_lost=False`, driver events none, 118 engine PNGs. No source change this cycle.

**Attribution (same `--tracy-start-seconds 110` window vs Dev 107
`d107-perdraw-tracy-bis-c-framedump-pooloff-20261006-103732`):**
`SynchronizeBuffersInRanges` 9,347.9 → 6,593.9 ms (**-29.5%**, 6.576% → 4.637%); frame counter
110–145 s **400/31.56 = 12.67 → 477/31.42 = 15.18 FPS (+2.51 / +19.8%)**, consistent with +13%
draws processed. Session `median_fps` (16) is phase/foreground variance, not usable.

**New finding — engine-presented-frame blackout:** per-frame darkness + contact sheets show
post-intro BLACK windows in 2 of 3 `bdagen` runs (`...-tracy...`: frames 82–108 maxrun 15;
`...-b-20261006-110044`: 170–186 maxrun 6) and **0 of 4** non-`bdagen` runs (all four Dev 107
sessions clean through long standing phases). Live fight HUD + running clock over hard-edged
black geometry replacing fighters/arena — the dirty-log mesh-explosion / user-blackout shape.
Association only; intermittent and phase-confounded, causation not established.

**Decision:** keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in / default OFF; do not default or
promote** while implicated in engine-frame corruption. Report:
`_Build/agent-reports/dev108-bdagen-tracy-attribution-20261006.md`.

**Next:** two `bdagen`-OFF Dev 108 runs (`d108-bis-c-framedump-pooloff`) + one repeat `bdagen`
run, comparing engine contact sheets (not a single frame) to confirm/exonerate; then the a–p
letter bisect only if exonerated. 25/30 FPS and smooth media unmet.

### UFC Dev 108 — generation-gated BDA sync, opt-in (2026-10-06, 11:07)

Consumed the completed per-draw profile `d107-perdraw-tracy-bis-c-framedump-pooloff-20261006-103732`:
`SynchronizeBuffersInRanges` (full BDA walk) was the top attributable engine cost, **6.58% /
9.35 s in 30 s / 311 ms/s** on the GPU command thread. Implemented a **safe** skip that does not
depend on dirty-log completeness: `MemoryTracker::CpuDirtyGeneration()` bumps on every CPU-dirty
transition (before the log-overflow early-return); `SynchronizeBuffersInRanges` skips the walk
only while nothing transitioned, no buffer registered, ranges version unchanged. Any non-quiescent
call still performs the full conservative walk (never the risky per-range dirty-sync), so a lost
log entry cannot cause a stale upload. Opt-in `KYTY_UFC_BDA_SYNC_GENERATION=1`, label `bdagen`.

Files: `src/graphics/host_gpu/memoryTracker.{h,cpp}`,
`src/graphics/host_gpu/renderer/cache/bufferCache.{h,cpp}`, `AutoFight.ps1`,
`tests/MemoryTrackerTests.cpp` (`TestCpuDirtyGeneration`). Report:
`_Build/agent-reports/dev108-bda-sync-generation-20261006.md`.

Worker build **oc20261006-104920-build = UFC Dev 108** (installed). Focused
**oc20261006-105131-tests memory_tracker 1/1**; full **oc20261006-105144-tests 55/55 (31.64 s)**.
Preflight clear before every serial job; heartbeat good; no inherited overrides.

Matched gameplay (2560x1440/Mailbox, validation/red-zone ON, framedump, pool OFF, c OFF):
- **oc20261006-105227-fight** → `_Build/autolab/d108-bdagen-bis-c-framedump-pooloff-20261006-105250`, exit 0, 422.0 s;
  `bda sync fast=445,768 walk=176,126` (28.3% walked), session avg 21.58, max RSS 11,141 MB.
- **oc20261006-110022-fight** → `...-b-20261006-110044`, exit 0, 421.6 s;
  `bda sync fast=387,338 walk=140,690` (26.6% walked), session avg 23.02, max RSS 11,176 MB.
- Control `d107-dcc-fixture-check-bis-c-framedump-pooloff-20261006-102543`: `fast=0 walk=810,893`,
  session avg 17.95.

Full BDA walks **78–83% lower** than the matched control; 71–73% of calls now skip the walk.
Both runs no device loss/driver events (benign SPIR-V cache banner only); engine frame
`frame_000184` shows a correct live ground fight. `FightWatch` false-negative preserved
(`fight_frame_rate`/`fight_frames` n/a), so no exact first-45-s or causal FPS claim. **Retain
as opt-in**; next: a `bdagen` Tracy capture to attribute the removed walk to FPS, then a manual
ground-and-pound visual check before considering it default. 25/30 FPS and smooth media unmet.


### UFC Dev 107 unchanged — DCC producer/predicate-ring regression verified (2026-10-06, 10:34)

Completed the planned source-driven CPU/GPU semantic fixture, ending negative-group
baseline work. Added `CheckColorMetadataProducerSequence` in
`tests/ShaderRecompilerComputeTests.cpp`: CPU 7/GPU **2,053 producer snapshots**,
**4,106 GPU slices**, exact pixels and metadata for selected/unselected layers,
fixed/rejected/nonuniform codes, predicate-ring wrap across nonblocking submission,
first-discovery rejected-clear image initialization and coherent publication followed
by a new GPU producer. **No semantic mismatch demonstrated.** No engine/default/harness
change, install or numbered build; preserved existing source work and pool-OFF quarantine.
Full evidence/fixture development corrections/limits:
`_Build/agent-reports/dcc-producer-sequence-20261006.md`.

Worker incremental focused **oc20261006-102318-tests 1/1**, **2.93 s**; final full
**oc20261006-102406-tests 55/55**, **31.38 s**. Earlier failures were fixture mistakes
(slice counter, padded linear guest storage), corrected without weakening the oracle.
Immediate preflight clear before every serial job; heartbeat passed, no inherited overrides.

Necessary unchanged-runtime check **oc20261006-102521-fight**, exit 0:
`_Build/autolab/d107-dcc-fixture-check-bis-c-framedump-pooloff-20261006-102543`, **421.51 s**.
Dev 107, configured 2560x1440/Mailbox, validation/red-zone ON, c/pool OFF,
a/b/d–p ON, 185 engine PNGs / requested 1 s Windows shots, no Tracy/shader dump.
Same fighters/arena; named HUD ~94.65–96.77 s, >300 s fighting, final clinch R1 0:45.
Periodic/final PNGs lack prior giant black occlusion; speckling/manual transient check remain.
Windows captures black; automatic first_fight_s/fight_frame_rate/fight_frames n/a preserved.
- Diagnostic **4,808/324.74 = 14.8057 FPS**, sampled fighting median/min **14/11**;
  opening **549/42.65 = 12.8722**, not exact first-45-s or causal gain. Session median/min **14/0**.
- Max RSS/device VRAM **11,204.5/12,676 MB**; entrance/loading **4.7059 FPS**, not codec-only proof.
- Final nominal waits cp/other/untagged/CPU-write **20.857/31.870/10.300/1.170 ms/s**
  (overlap); metadata/indirect drains absent; GPU slices **2,447**, fills CPU/GPU
  **11,205/9,473**, GPU indirect **21,072**, BDA fast=0, pool zero. No loss/triaged failure.
Dump readbacks/phase mix affect timing; foreground acquired once, not continuously verified.

Retain regression, no speculative engine correction or a/b default verdict. **25/30 FPS and
smooth media unmet. Next independent current-PC per-draw/readback profile**, one 30 s Tracy
capture explicitly timed at ~110 s (FightWatch false-negative), PNG-verify phase:
`py -3.13 RunnerJob.py fight --label d107-perdraw-tracy-bis-c-framedump-pooloff --monitor-seconds 300 --tracy-seconds 30 --tracy-start-seconds 110`.
Then one evidenced engine-work reduction with invalidation/producer-order proof, not more
negative group baselines or repeated passing DCC fixture. No input pause/routine approval.

### UFC Dev 107 unchanged — SRT memo restored, two sampled-negative captures; end negative-baseline branch (2026-10-06, 10:10)

Completed pending **a–c OFF / d–p ON / pool OFF** discriminator and repeat.
**No prior giant black geometry/live-HUD occlusion in reviewed engine PNGs of either
valid run.** SRT reuse active (384,063/355,401); not shadow-verified. No culprit or
fix verdict: a/b-OFF metadata drains may mask races. Full evidence/next fixture:
`_Build/agent-reports/dev107-abc-discriminator-20261006.md`.

No engine/default/harness change, build or CTest. Focused pool/allocation and full
**55/55** inherited, not rerun. Preserved existing source work. Changed report, latest
HANDOFF/LOOP-LOG, generated contact PNG/JSON via existing CPU analysis. Immediate
preflight clear before each serialized worker job; heartbeat passed, no inherited overrides.
- **oc20261006-095133-fight**, exit 0: `_Build/autolab/d107-bis-abc-framedump-pooloff-20261006-095157`, **421.01 s**.
- **oc20261006-095953-fight**, duration 0 / guest-address-space reservation failure,
  excluded; waited 30 s and repeated preflight before retry, not an engine fault.
- **oc20261006-100111-fight**, exit 0: `_Build/autolab/d107-bis-abc-framedump-pooloff-b-retry-20261006-100132`, **421.11 s**.
Both Dev 107, configured 2560x1440/Mailbox, ShaderValidation/red-zone ON, 186 engine
PNGs each, requested 1 s Windows shots, no Tracy/shader dump. Source 3840x2160/PNG
960x540; no rendering/settings reduction; foreground acquired once, not continuously verified.

Same fighters/arena, advancing EA imagery; named HUD ~96.6–96.7 s, >300 s fighting.
A late ground HUD 0:41→0:20; B standing 0:54→0:37, fighters/scene visible. Windows
shots all black, automatic first_fight_s/fight_frame_rate/fight_frames n/a false negatives
preserved. Sparse review cannot certify transients; speckling/manual check remain.
- Conservative sampled fight A/B **5,451/324.30 = 16.8085**, **4,992/324.51 = 15.3832 FPS**;
  fighting sampled median/min **15/11**, **14/11**. Common ~42 s opening **12.9983/13.1840**,
  not exact first-45-s qualification or FPS gain; late workload differs/readbacks perturb timing.
- Session median/min **15/0**, **14/5**, max RSS/device VRAM **11,041.2/12,578 MB**,
  **11,036.8/12,454 MB**. Entrance/loading **4.9843/5.0608 FPS**, not decoder-only proof.
- Final nominal waits ms/s metadata **78.093/80.707**, cp **1.403/1.353**, other
  **6.053/6.220**, untagged **6.677/6.810** (overlap). GPU indirect **21,853/21,573**,
  BDA fast=0, pool 0. No device loss/triaged errors except benign validation-cache banner.
  Final translate **2392/2389 ms**, cumulative not movie-only.

Decision: retain unchanged defaults/pool-OFF quarantine; **25/30 FPS unmet**, smooth
movie/audio/codec unverified. **End negative-baseline branch. Next source-driven CPU/GPU
DCC/fill semantic fixture**, not another negative group control: extend existing color-
metadata GPU/CPU tests for producer changes, nonuniform slices, selected-layer preservation
and predicate-ring wrap; compare target pixels AND expanded bytes. Review GPU evaluation
before InitializeImage/conditional clear and coherent-fill publication; no defect proven yet.
If inconclusive, independent intro/readback/per-draw work, no input pause or routine approval.

### UFC Dev 107 unchanged — repeated a–d-OFF captures lack prior black geometry; separate d next (2026-10-06, 09:49)

Completed one bounded smaller-group discriminator. **No prior giant black geometry/live-HUD
scene occlusion in reviewed periodic and individual engine PNGs of either completed run.**
Restored e–h versus previous a–h-OFF controls; a–d/pool remain OFF. Not a culprit/fix verdict:
restored metadata drains and late standing/ground differences may mask intermittent races.
Full evidence: `_Build/agent-reports/dev107-abcd-discriminator-20261006.md`.

No engine/default/harness edit, build or CTest. Focused pool/allocation and full **55/55**
inherited, not rerun. Existing uncommitted work preserved. Changed report, this section,
LOOP-LOG; reused CPU analysis for contact PNG/JSON. Ownership/env preflight clear immediately
before serialized worker jobs; heartbeat passed, no inherited KYTY_UFC overrides.
- **oc20261006-093034-fight**, exit 0: `_Build/autolab/d107-bis-abcd-framedump-pooloff-20261006-093055`, **420.78 s**.
- Repeat **oc20261006-093830-fight** had duration 0 / guest-address-space reservation failure;
  excluded, waited 30 s and rechecked ownership before retry, not an engine fault.
- **oc20261006-093948-fight**, exit 0: `_Build/autolab/d107-bis-abcd-framedump-pooloff-b-retry-20261006-094009`, **420.79 s**.
Both Dev 107, configured 2560x1440/Mailbox, validation/red-zone ON, **a–d OFF, e–p ON,
pool OFF**, 187 prepared PNGs each, requested 1 s Windows shots, no Tracy/shader dump.
Source dump 3840x2160/PNG 960x540, no setting/quality reduction; foreground acquired once.

Same Makhachev black/Della Maddalena green, T-Mobile Arena, advancing EA imagery; HUD ~92–96 s,
>300 s fighting. A late standing then ground, B late ground. A 175 HUD 0:47 and 186 HUD 0:26,
B 186 HUD 0:05 render fighters, no prior occlusion. Windows shots remain black; automatic
first_fight_s/fight_frame_rate/fight_frames n/a false negatives preserved. Sparse review cannot
certify transients; speckling/manual check missing. Smooth media/audio/codec unverified.
- Conservative sampled fight A/B **5,215/324.25 = 16.083 FPS**, **5,894/324.05 = 18.189 FPS**;
  fighting sampled median/min **15/12**, **17.5/12**. Common ~42 s opening **16.958/13.632 FPS**,
  not exact first-45-s qualification or a causal gain. Whole-session median/min **15/1**, **18/0**.
- Max RSS/device VRAM **11,057.6/12,469 MB**, **10,898.4/12,499 MB**. Entrance/loading
  **5.359/5.429 FPS**, not decoder-only. Final nominal waits ms/s metadata **87.997/122.263**,
  cp **1.523/2.307**, other **6.430/8.877**, untagged **7.893/9.587** (overlapping).
  GPU indirect **22,141/23,138**, BDA fast=0, SRT memo/pool zero; no device loss/triaged error
  beyond benign validation-cache banner. Final translation **2357/2365 ms**, not movie-only cost.

Decision: retain unchanged defaults/pool-OFF quarantine, **25/30 FPS unmet**. Next bounded
**`d107-bis-abc-framedump-pooloff`**, 420 s / 1 s shots restores only d/SRT memo. Positive
prioritizes memo invalidation regression; negative gets one repeat then direct a/b CPU-vs-GPU
clear/fill semantic work. If inconclusive, independent intro/readback/per-draw work, no input pause.

### UFC Dev 107 unchanged — two a–h-OFF sampled controls lack prior live-HUD black geometry (2026-10-06, 09:22)

Completed the pending smaller-feature discriminator with a matching repeat, all via worker.
**No giant black geometry/live-HUD occlusion in reviewed periodic/late engine PNGs in either
run.** This is bounded sampled negative evidence, not a culprit verdict or an immediate-load
fix. Late workload differs (more ground fighting), and disabling a/b restores substantial
metadata drains that may mask timing races. Full evidence/limitations/next command:
`_Build/agent-reports/dev107-abcdefgh-discriminator-20261006.md`.

No engine/default/harness change, build or CTest; focused pool/allocation and full **55/55**
inherited, not rerun. Existing source work preserved. Changed this section, LOOP-LOG and report;
existing CPU analysis generated per-session contact PNG/JSON. Preflight ownership/env clear
immediately before each serialized job, heartbeat passed, no inherited KYTY_UFC overrides.
- **oc20261006-090458-fight**, exit 0: `_Build/autolab/d107-bis-abcdefgh-framedump-pooloff-20261006-090520`, **421.11 s**.
- **oc20261006-091315-fight**, exit 0: `_Build/autolab/d107-bis-abcdefgh-framedump-pooloff-b-20261006-091339`, **421.55 s**.
Both Dev 107, configured 2560x1440/Mailbox, validation/red-zone ON, **a–h OFF, i–p ON, pool OFF**,
186 engine PNGs each, requested 1 s Windows shots, no Tracy/shader dumps. Dump source 3840x2160,
diagnostic PNG 960x540; no resolution/quality change. Same Makhachev black/Della Maddalena green,
T-Mobile Arena, advancing opening imagery. HUD ~94–97 s; >300 s actual fighting visible.

Windows shots still black; automatic first_fight_s/fight_frame_rate/fight_frames n/a false
negatives preserved. A final frame 185 entirely black **without HUD**, not counted as the
prior failure, and excluded by conservative window. B late frames 175/179/185 render ground
fighting with HUD **0:36/0:27/0:13**. Sparse review does not certify transients; speckling remains.
- Conservative ≤400 s windows A/B: **5,803/303.19 = 19.140 FPS**, **5,119/301.38 = 16.985 FPS**;
  fighting sampled median/min **20/12**, **15/11**. Common sampled ~42 s opening **13.628/14.172 FPS**,
  not exact first-45-s qualification. Foreground acquired once, phase mix differs; no FPS gain.
- Session sampled median/min **21/1**, **15/0**; max RSS/device VRAM **11,259/12,648 MB** and
  **11,066.4/12,517 MB**. Entrance/loading **5.383/5.002 FPS**, not codec-only evidence.
- Last nominal wait ms/s A/B: metadata **142.937/130.187**, cp **2.020/1.840**, other
  **496.877/461.337**, untagged **4.437/7.937** (overlap). GPU indirect **23,893/22,698**;
  BDA fast=0, SRT memo/pool counters zero. No device loss/FATAL/validation error found.

Decision: retain unchanged defaults and pool-OFF quarantine, no unconditional clean verdict.
Next **`d107-bis-abcd-framedump-pooloff`**, 420 s / 1 s shots, restores e–h while c/pool stay OFF;
positive motivates e–h/interactions, negative needs matching repeat before narrower diagnosis.
If narrowing is inconclusive, advance independent intro/per-draw work, not another input pause.
Manual transient check missing; smooth media/audio/codec unverified; **25/30 FPS unmet**.

### UFC Dev 107 unchanged — black geometry reproduces with BOTH pool and dirty log OFF (2026-10-06, 09:0x)

Completed the pending pool-OFF/c-OFF discriminator. **Positive:** prepared-frame 175 (~400 s)
has black geometry over Della Maddalena's head, 178/179 huge black shapes across fighters,
185 most scene occluded while HUD advances **R1 0:44 → 0:27**. **Neither image pooling nor
dirty log is required for this reproduced failure.** Late onset, not proof of user's immediate-load
timing. Do not call pool or c the sole culprit or disabling them a fix. Pool quarantine/default
OFF retained, no cap tuning. Full evidence/source-review boundary and next command:
`_Build/agent-reports/dev107-pooloff-dirtylogoff-positive-20261006.md`.

No engine/default/harness edits, build or new tests. Focused pool/allocation and full **55/55**
inherited, not rerun; source work preserved on `dev`. Changed this section, LOOP-LOG and report;
existing CPU analysis generated contact sheet/JSON. Ownership/env preflight clear immediately
before both worker jobs, heartbeat passed, no inherited KYTY_UFC variables; all serial.
- `oc20261006-085042-fight`: zero-duration guest-address-space reservation failure, excluded.
- Retry after wait/clear preflight: **oc20261006-085148-fight**, worker exit 0, session
  `_Build/autolab/d107-bis-c-framedump-pooloff-retry-20261006-085210`, **421.38 s**, 186 engine PNGs.
  2560x1440/Mailbox, red-zone/ShaderValidation ON, only c OFF among a-p, pool OFF, frame dumps +
  requested 1 s Windows shots; no Tracy/shader dumps. BDA `fast=0 walk=650465`, pool counters all 0.

EA imagery progresses; same Makhachev black / Della Maddalena green, T-Mobile Arena,
standing/clinch/ground/standing. Stamina bars ~94.57 s, full named HUD ~96.69 s. Windows shots
all black, automatic first_fight_s/fight_frame_rate/fight_frames n/a remain false negatives.
**Additional false negative:** generated JSON has no >85%-dark candidates, yet direct engine
images prove corruption (frame 185 only **79.10%** dark below HUD, right cage/canvas visible).
Do not use that threshold to qualify visuals.
- Conservative pre-observed-failure **96.69–398.04 s: 5,236 / 301.35 = 17.375 FPS**, sampled
  fighting median/min **15/11**. Full **5,568 / 324.69 = 17.149 FPS includes failure**, not acceptance.
- Common opening **571 / 42.47 = 13.445 FPS**, not exact first-45-s window. Session median/min
  **15/5**, max RSS/device-wide VRAM **11,221.2 / 12,651 MB**. Entrance ~54–80 s **4.637 FPS**,
  loading/3D, not proven decoder throughput. Foreground acquired once, phases not matched controls.
- Final nominal waits cp/other/untagged **28.987/41.763/11.917 ms/s** (overlapping attribution),
  no metadata/indirect drains, GPU indirect **22,218**. No device loss/FATAL/validation errors;
  benign validation-cache banner only. Dump readbacks perturb timing; not an FPS improvement.

Next concrete discriminator: **`d107-bis-abcdefgh-framedump-pooloff`**, 420 s, 1 s shots through
RunnerJob, direct prepared-frame review. A positive excludes a-h as required; negative needs
matching repeat. Then targeted buffer-publication/BDA/indirect or shader regression with actual
draw/producer identity, not further pool-only qualification. Source review found no established
early-free defect; handle-only fixture insufficient. Speckling/manual check missing, smooth media/
audio/codec unverified, **25/30 FPS unmet**. No missing-input pause, supervisor/agent, commit/push.

### UFC Dev 107 unchanged — live-HUD black mesh/occlusion reproduces with dirty log OFF; pool still fails acceptance (2026-10-06, 08:4x)

Focused bounded separation diagnostic for the latest pool-ON failure. No engine/default/harness
edit, build or CTest; previous focused `host_image_pool`/`host_image_allocation` and full **55/55**
inherited, not rerun. Existing source preserved, branch `dev`. Changed HANDOFF/LOOP-LOG,
`_Build/agent-reports/dev107-pool-dirtylog-separation-20261006.md`, and the existing CPU analysis
script to accept session args; generated JSON/contact sheets. Full evidence/reproduction in report.

Ownership/env preflight clear immediately before each serialized worker job; heartbeat passed.
2560x1440/Mailbox, red-zone/ShaderValidation ON, no Tracy/shader dump, prepared frames + requested
1 s Windows shots. **Image pool ON, unchanged 512 MiB cap; only c / dirty log OFF; other a-p ON.**
- `oc20261006-082600-fight`, exit 0:
  `_Build/autolab/d107-imgpool-bis-c-framedump-20261006-082623`, **422.04 s**, 186 engine PNGs.
- Repeat `oc20261006-083435-fight`: duration 0, exit_code 321, guest-address-space reservation
  failure before gameplay; excluded. Waited and retried after clear preflight, not an engine fault.
- `oc20261006-083533-fight`, exit 0:
  `_Build/autolab/d107-imgpool-bis-c-framedump-b-20261006-083555`, **421.29 s**, 187 engine PNGs.

EA opening imagery progresses; Makhachev black/Della Maddalena green, T-Mobile Arena; HUD ~92–95 s.
All Windows shots remain black, so automatic first_fight_s/fight_frames/fight_frame_rate n/a are
false negatives. A has no post-frame-37 near-black candidates and reviewed periodic samples
show standing/ground/standing. **B fails: frame 178 (~404 s) giant opaque black polygons; 179–186
>85% dark.** Individually reviewed frame 182 HUD 0:34 and 186 HUD 0:26 advance while black mesh
occludes 3D with some cage/canvas visible. Not a HUD-absent cutscene. This rules out dirty log
being **required** for this failure; it doesn't prove pool causality or match immediate-load timing.

Diagnostic throughput (sampled frame-counter windows, not clean acceptance):
- A **97.07–422.04: 5,861 / 324.97 = 18.036 FPS**, fighting median/min **15/12**.
- B pre-failure **96.61–397.93: 5,177 / 301.32 = 17.181 FPS**, median/min **15/11**.
  Full **5,708 / 324.68 = 17.580 FPS includes failure**, not valid visual performance.
- Common opening ~42 s **12.796 / 12.807 FPS**, not exact first-45-s acceptance; foreground
  acquired once, workload phases differ. Session median/min A **16/0**, B **16/1**.
- Max RSS/device VRAM A **11,246.7 / 13,073 MB**, B **11,212.1 / 13,221 MB**.
- Final nominal cp/other/untagged waits A **23.47/33.27/9.23 ms/s**, B **43.08/52.89/5.48**;
  attribution overlaps, no metadata/indirect drains. GPU indirect active 23,471/22,221 draws.
- BDA fast=0, walks **590,139/659,664** prove c OFF. Pool hits/misses/evictions A
  **54,000/116,333/114,321**, B **55,980/110,239/108,143**, ~31.70/33.68% native creates avoided;
  not an FPS gain. No device loss/FATAL/validation error; benign cache banner only.

Decision: **pool still rejected for qualification, default OFF preserved; c OFF is not a fix.**
Do not increase pool cap or claim dirty-log culprit. Readbacks perturb timing; sparse PNGs don't
certify transient visuals. Speckling/manual check missing; smooth media/audio/codec unverified.
**25/30 FPS unmet.** Next bounded discriminator: `d107-bis-c-framedump-pooloff`, 420 s / 1 s shots
(no `imgpool` token), same dirty-log OFF but pool OFF. Then use source-driven GPU pattern /
retirement / recycled-image initialization / image-to-buffer-BDA visibility regression; current
handle-only pool fixture doesn't prove these semantics. No draw/shader identity captured yet.
No missing-input pause, new supervisor, commit or push.

### UFC Dev 107 unchanged — pool qualification captures real mesh explosion and live-HUD blackout; unchanged pool-OFF control lacks that failure (2026-10-06, 08:19)

Focused necessary acceptance of the existing opt-in native-image pool; no engine/default/harness
edits, build or tests this cycle. Previous focused `host_image_pool`/`host_image_allocation`
and full **55/55** inherited, not rerun. Existing work preserved; branch `dev`. Ownership/env
preflight clear immediately before each worker job, heartbeat passed; red-zone and validation ON.
Details/reproducible CPU analysis and contact sheets:
`_Build/agent-reports/dev107-imgpool-qualification-20261006.md` and
`dev107-pool-qualification-analysis.py`. Changed these reports plus HANDOFF/LOOP-LOG only.

Worker jobs, both exit 0, 2560x1440/Mailbox, requested 420 s, no Tracy/shader dumps,
1 s Windows screenshots plus prepared-frame diagnostics, all a-p ON:
- **oc20261006-080316-fight**, pool ON at unchanged 512 MiB cap:
  `_Build/autolab/d107-imgpool-framedump-20261006-080338`, recorded **419.06 s**.
- **oc20261006-081131-fight**, unchanged-code pool OFF control:
  `_Build/autolab/d107-framedump-blackout-control-20261006-081151`, **420.90 s**.

Both automated first_fight_s/fight_frame_rate/fight_frames remain n/a (black Windows capture
false negatives). Engine frames show EA opening progression and Makhachev black shorts vs
Della Maddalena green, T-Mobile Arena; stamina bars frame 36/full HUD frame 37 (~90–93 s).
Foreground acquired once, not continuously checked. No movie smoothness/audio/codec claim.

**Actual positive:** pool ON frame 60 (~143 s) has a huge skin-textured mesh triangle.
Frames **140/141 (~320/323 s)** show almost black 3D scene with live HUD; frame 143 timer
advances and frame 160 shows giant black occlusion with some fighter/arena visible. 14/134
post-frame-37 engine PNGs exceed 85% scene darkness. New OFF control reviewed through frame
177 (~400 s) shows ground/submission fighting without comparable failure; its only dark
candidate, frame 178, is a **HUD-absent between-round transition**, not live-HUD blackout.
Original OFF reference has no such dark candidates. This does not prove cause or equivalence
to the user's immediate-load blackout; dirty-log remains ON and previously caused explosions.

Conservative diagnostic metrics from frame counters (raw summaries preserved):
- ON **96.77–419.06 s: 4,252 / 322.29 s = 13.193 FPS**, median/min **16/0**;
  late **323.28–419.06 s: 504 / 95.78 = 5.262 FPS**.
- OFF control **97.04–399.63 s**, excluding corner scene: **7,047 / 302.59 = 23.289 FPS**,
  fighting sampled median/min **24/16**, fight-span RSS/VRAM **11,189.8 / 12,711 MB**.
- Common sampled opening ~97–140 s: original OFF **15.864**, ON **15.760**, new OFF **17.774 FPS**;
  ~42 s samples, not exact first-45-s qualification windows. Workloads diverge: ON returns to
  standing while new OFF stays mostly ground, so no causal whole-fight percentage FPS claim.
- ON max RSS/device VRAM **11,097.2 / 13,305 MB**; OFF whole-session **11,609.1 / 12,974 MB**,
  session median/min **24/1**. No device loss/FATAL/validation errors; benign cache banner only.
- ON final pool hits/misses/evictions **37,897 / 111,952 / 109,659**, recycled **372,630.5 MiB**:
  ~25.29% measured CreateImage requests avoid native allocation, not an FPS gain.
- Nominal ON final waits cp_read_fault **791.54 ms/s**, other_threads **775.54**, untagged **6.28**
  versus pre-failure **28.90/40.32/8.85**; OFF final **47.79/60.18/1.80** (includes corner phase).
  Waits overlap across threads. Metadata/indirect blocking absent; GPU indirect remains active.

Decision: **failed pool visual qualification; reject default enabling/cap tuning.** Preserve
existing opt-in code/tests default OFF for diagnosis, not as an accepted improvement. Sparse
PNG diagnostic reads perturb timing; speckling remains, manual transient-artifact check absent.
**25/30 FPS unmet.** Next bounded hypothesis: stale vertex upload/lifetime interaction. Repeat
`d107-imgpool-bis-c-framedump` with only dirty log disabled (420 s, same diagnostics), or first
write a targeted native-image reuse/upload fixture if source proves a defect. Need repeated
negative controls before a culprit verdict; current runs lack failing draw/shader identity.
No wait-for-input request, new supervisor, commit or push.

### UFC Dev 107 unchanged — engine frames prove Windows capture false negatives; EA opening and >328 s actual fighting visible, 19.05 diagnostic FPS (2026-10-06, 08:0x)

Focused milestone: validate the black-window measurement before blaming render paths or
tuning the retained image pool. Reused the existing timeline-completed prepared-frame
readback (`KYTY_UFC_FRAME_DUMP`); no engine/harness code changes, build or CTest this cycle.
Prior build's focused `host_image_pool`/`host_image_allocation` and full **55/55** are inherited,
not newly executed. Only `HANDOFF.md`/`LOOP-LOG.md` changed this cycle. Existing work preserved.

Worker job **oc20261006-075042-fight**, exit 0, session
`_Build/autolab/d107-framedump-capture-validation-20261006-075104`, **421.45 s**;
2560x1440/Mailbox, ShaderValidation and red-zone ON, no Tracy/shader dumps, all a-p defaults
ON, **image pool OFF** (confirmed hits/misses/evictions all zero). Preflight found no manual
launcher/emulator or competing build/test owner and no inherited `KYTY_UFC_*` overrides;
runner heartbeat check passed. Foreground acquisition logged `fg=True` once, not continuous
foreground verification.

Concrete finding: **197/197 Windows shots were dark, yet 187 prepared-frame PNGs were written**.
Directly reviewed selected engine frames: 000004/5/6 show changing EA opening animation/logo;
000025 shows dark arena entrance with lights and Skip prompt; 000030 is a black transition;
000031-34 show the referee/arena without HUD; **000035 (07:52:35.871 write time, approximately
91 s after launch)** shows gameplay HUD and fighters. 000040/60 show standing/clinch,
000080/100 show ground fighting, 000120/140/160 standing, and 000180/186 ground/submission
(last frame 07:58:06.373). Makhachev (black shorts) vs Della Maddalena (green), T-Mobile Arena.
Thus `fight_detected=False` / `first_fight_s=n/a` are **capture-detector false negatives** here,
not evidence of a non-fight or engine-black scene. Earlier black-window runs likewise cannot
be called menu-only/non-fights solely from that detector. This does not diagnose or disprove
the user's separate immediate-load live-HUD blackout, nor prove every dumped frame clean.

Independent frame-counter calculation from `metrics.csv` (do not rewrite its raw summary):
- Conservative HUD-confirmed span **92.49-421.45 s: 6,266 frames / 328.96 s = 19.048 FPS**.
- Opening sampled window **92.49-136.97 s: 706 frames / 44.48 s = 15.872 FPS** (not an exact
  fight-triggered 45 s acceptance window).
- **200.61-240.95 s ground window: 972 frames / 40.34 s = 24.095 FPS**; standing returns later.
- Entrance/loading trough **54.33-79.76 s: 139 frames / 25.43 s = 5.466 FPS**; contains loading
  and 3D entrance, not a proven movie-decoder-only workload.
- Session sampled median/min **18/1 FPS**, max RSS **11,100.3 MB**, max device-wide VRAM
  **12,768 MB**. Automated fight-only median/min/RSS/VRAM remain n/a. No device loss/FATAL;
  triage's sole SPIR-V match is the benign known-valid cache banner.
- Nominal final 30 s wait deltas: `cp_read_fault` **36.84 ms/s**, `other_threads` **50.60 ms/s**,
  `gpu_untagged` **10.91 ms/s**; metadata/indirect blocking sites absent, GPU indirect active
  (24,549 cumulative). Early nominal ~30-60 s interval: cp **79.39**, other **61.77**, untagged
  **41.90 ms/s**. Do not sum overlapping thread-attributed waits.

Decision: retain unchanged Dev 107 and default-OFF pool; diagnostic resolves the capture
ambiguity, **not an optimization/25-FPS acceptance**. Engine PNGs are reduced-size diagnostics
(guest sources include 3840x2160; configured host remains 2560x1440), not a resolution change.
EA opening imagery progresses, but audio, smooth movie playback and codec attribution are
unverified. Skin/cage/canvas speckling remains visible; manual transient-artifact check missing.
Sparse prepared-frame dumps perturb presentation and cannot certify momentary mesh stability.
**25/30 FPS remain unmet.**

Next concrete action: use this verified standing/clinch/ground reference instead of assuming
black Windows shots mean no fight. Qualify the existing opt-in image pool with a scenario-matched
`d107-imgpool-framedump` run before tuning its cap or enabling it; compare actual frame windows,
pool counters and VRAM with this pool-OFF control, repeat comparable controls if gain is small.
For independent media work, early profiling must distinguish the now-visible EA animation
from the 54-80 s 3D entrance/loading trough; do not infer a VP9 bottleneck from binary strings.
No new supervisor, wait-for-input request, commit or push.

### UFC Dev 107 — opt-in native-image backing pool cuts vmaCreateImage/vmaDestroyImage calls ~25%, but thrashes under the 512 MiB cap; automated run was a black non-fight so no gameplay FPS (2026-10-06, 07:4x)

Cycle goal (overnight plan §3/§4, ENGINE-REVIEW P5; Dev 105 next hypothesis): attack the
command thread's ~11.5% native image create/destroy churn (`Image::~Image`+`DeleteImage`
~535 destroys/s) with a bounded backing pool instead of a cache-policy change.

Engine change (opt-in, default OFF): `GraphicContext` keeps freed `VkImage+VmaAllocation`
backings in `m_image_pool` keyed by the full Vulkan create parameters; `CreateImage`
recycles a match with layout reset to the request's `initialLayout`, `DeleteImage` pools
instead of `vmaDestroyImage`. Byte cap 512 MiB (`KYTY_UFC_IMAGE_POOL_BYTES`) and count cap
2048 (`KYTY_UFC_IMAGE_POOL_COUNT`); `DestroyAllocator` flushes the pool. Enable via
`KYTY_UFC_IMAGE_POOL=1` (AutoFight label `*imgpool*`). Files: `graphicContext.h`, `vma.cpp`,
`renderer/masterSemaphore.h/.cpp` (30 s `image pool hits/misses/evict/recycled_mb` line),
`tests/ShaderRecompilerComputeTests.cpp` + `CMakeLists.txt` (`host_image_pool`), `AutoFight.ps1`.

Verification: builds **UFC Dev 106/107** OK; new focused test `host_image_pool` passed
(identical descriptor recycles the handle with fresh sync state; a different descriptor does
not) plus `host_image_allocation`; full CTest **55/55**. Fight `d107-imgpool-a-20261006-074140`
(421.8 s, 2560x1440/Mailbox, ShaderValidation + red-zone ON, no Tracy, no device loss, no
FATAL, max RSS 11,270 MB).

Measured (pool ON, 420 s menu/streaming session):
- **Native driver calls reduced ~25%**: `image pool hits=51,162 misses=153,571 evict=151,889
  recycled_mb=526,111`. Total create/destroy ≈ hits+misses = 204.7k → creates 153.6k,
  destroys ≈153.6k, versus the baseline ~204.7k each (−25%).
- Hit rate only 25% because the **512 MiB byte cap is far below the working set** (avg recycled
  size ≈10 MB → ~50 backings); ~75% of pooled backings are evicted before a matching request.
  The cap, not the key, is the binding constraint; the switch is env-tunable.
- **No FPS/visual claim:** `fight_detected=False`, `fight_frames=n/a`; the 68/68 screenshots
  show a black client area while the title reported fps 30 / frames incrementing (max title
  frames 5,494). This matches the overnight plan's automated black window; menu-window fps
  median 19, peak 30. Not enough to judge the pool's gameplay effect.

Retain: opt-in pool + `host_image_pool` test + counters, **default OFF** (unproven in a real
fight). 25/30 FPS unmet.

Next hypothesis (grounded): (a) separate the automated black window from the user's live
blackout by dumping engine-presented frames (`*framedump*`, `KYTY_UFC_FRAME_DUMP`, independent
of the compositor) before touching render paths; (b) if the pool is pursued, raise the byte cap
for a real-fight A/B and/or retain only hot create-parameter classes (LFU) instead of evicting
by recency.

### UFC Dev 105 — image create/destroy churn attributed: ~95% is "no cached image" acquire (not layout expand/replace); texture GC is not the source (2026-10-06, 07:3x)

Cycle goal (overnight plan §3/§4, ENGINE-REVIEW P5 "find creation sources before changing
policy"): the #2 un-attacked command-thread leaf is native image create/destroy churn (Dev 102:
`Image::Image` 57 us + `Image::~Image` 102 us + `DeleteImage` 119 us, 16,047 destroys/30 s ≈ 535/s
= 11.5%). Attribute the creating decision before touching cache policy.

Engine change (diagnostic, additive, always on): `WaitStats::NoteImageCreate(reason)` +
`image_creates[6]` in the 30 s `GPU waits` line; reasons tagged at the five `InsertImage` call
sites in `textureCache.cpp` (0 null, 1 stencil, 2 acquire, 3 expand, 4 replace, 5 other). Files:
`masterSemaphore.h/.cpp`, `textureCache.cpp`. No behavior change.

Verification: build **UFC Dev 105** OK; full CTest **54/54**. Fight
`d105-image-churn-attr-a-20261006-072857` (301.0 s, 2560x1440/Mailbox, ShaderValidation+red-zone ON,
no Tracy, no device loss/FATAL, RSS 11,032 MB, VRAM 12,380 MB; `fight_detected=False` — a
menu/streaming-load session, not a measured fight).

Measured: final 30 s line `image creates null=2 stencil=22270 acquire=132115 expand=237
replace=2312 other=0`. Per-30 s `acquire` +12.8k–17.0k (~470–570/s), `stencil` +2.4k–3.8k
(~85–127/s), `expand` ~0 → **~95% of native image creation is the "no cached image in this range"
acquire path (FindImage line ~1454) + stencil associations**; expand/replace negligible (0.2%).
`images gc_freed=0` all run → texture GC is not the source; frees are acquire/overlap evictions
then re-creation. Consistent with the Dev 102 fight Tracy (~535/s during fighting).

Retain: attribution counters. No FPS change claimed; 25/30 FPS unmet.

Next hypothesis (grounded): bounded, opt-in native-image backing pool keyed by the Vulkan create
parameters in `GraphicContext::CreateImage/DeleteImage` (vma.cpp); recycle freed
`VulkanImage+allocation` with layout reset to `eUndefined`, VRAM- and count-capped, behind
`KYTY_UFC_IMAGE_POOL` (default OFF) with pool hit/recycled-byte counters. Frees already run after
the owning timeline tick, so recycled backings are not in flight.

### UFC Dev 103/104 — SRT invariant-phi cache is a measured no-op (`srt phi computed=0`); SRT plans never evaluate Phis, so the #1 leaf is pure per-draw value evaluation (2026-10-06, 07:2x)

Cycle goal (overnight plan §3 / Dev 102 next hypothesis): cut the command thread's #1 leaf
`SrtRefreshFlatBuffer` (7.03 s / 28.38 s = 24.8% in the Dev 102 fight Tracy). Cross-draw reuse
is already implemented (`KYTY_UFC_SRT_MEMO`, 1.9–2.3% hit) and capped by per-draw `user_data`
changes, so the only remaining redundant computation in that leaf was the structural
`ResolveInvariantPhi` BFS (heap scratch) recomputed on every `EvaluatePhi`.

Engine change (candidate): cached the structural invariant-phi resolution per `ResourcePlan`
keyed by the dense evaluation index, plus a `srt phi cached/computed` counter in the 30 s
`GPU waits` line. Files: `ShaderIR.h`, `ir/Program.cpp`, `ir/passes/SrtWalker.cpp`,
`renderer/masterSemaphore.cpp`, `tests/ResourceMaterializationTests.cpp`.

Verification: build **UFC Dev 103** OK; focused `resource_materialization` passed; full CTest
**54/54**; fight `d103-srt-phi-cache-a-20261006-071111` (421.9 s, 2560x1440/Mailbox,
ShaderValidation + red-zone ON, no Tracy, no device loss, no FATAL, RSS 11,340 MB).

Measured:
- **`srt phi cached=0 computed=0` for the entire 420 s run** — `SrtWalker::EvaluatePhi` is never
  invoked during materialization. UFC SRT value graphs contain no evaluated Phis (diamond phis
  are lowered to host `SelectU32` by `ResourceTracking`), so the cached path is cold. The change
  is a measured no-op and was **reverted**.
- `srt memo reused=448,533 / inputs=22,574,401` (1.9% hit), `replayed_reads=339` — re-confirms
  the whole-snapshot SRT memo is capped by per-draw `user_data`; cross-draw reuse is not a lever.
- Frame-counter windows (frame column, display off): 40-80 s **11.47**, 80-120 s **15.62**,
  120-160 s **16.73**, 160-200 s **17.57**, 200-240 s **26.15**, 240-280 s **26.17**,
  280-320 s **22.21**, 320-360 s **19.09**; median 21, peak ~26. Workload timing differs from
  Dev 102 (best window 27.85), so no gain or regression is claimed.
- `bda sync fast=695,789 walk=20,462 verify_missed=0`; `images gc_freed=0`; `buffers created=13,944`.

Revert/retain: reverted (build **UFC Dev 104** — same source as Dev 102 for this path; focused +
full CTest **54/54**). No retained engine change this cycle. 25/30 FPS unmet.

Next hypothesis (grounded by this result): SRT per-draw evaluation is fundamental (~30 value
evaluations per shader per draw at ~100 ns each, ~1.28 M materializations / 30 s). The next
un-attacked measured leaves are image create/destroy churn (Dev 102: `DeleteImage` 13,567
calls/30 s at ~119 µs = 11.5%; `Image::Image` ctor ~57 µs) and per-draw descriptor/binding prep
(`PrepareGraphicsBindings` + `CommitBindings` ≈ 6.5 s = 23%). A longer-term SRT win needs a
compact precompiled host evaluation plan, not another cache. Do not retry phi caching.

### UFC Dev 102 — thread-role registry proves the limiter is the GPU command thread, user-space compute-bound; new leaf ranking (SRT refresh 24.8%) (2026-10-06, 06:5x)

Cycle goal (overnight plan §3 / Dev 101 next hypothesis a): Dev 101 found the busiest thread at
0.88 cores but could not name it (`GetThreadDescription` returned nothing), leaving it unproven
whether to attack per-draw command-thread CPU work or cross-thread serialization.

Engine change (diagnostic; the sampler is already default-off): `SetCurrentThreadName` also
registers `GetCurrentThreadId() -> role` in a process-local map the sampler reads first (the Win32
thread description is kept as fallback), the sampler now prints each top thread's kernel/user CPU
split, and the `std::wstring` off-by-one in the description path was fixed. Files:
`src/common/threads.cpp`.

Verification: build **UFC Dev 102** OK; full CTest **54/54**. Fight
`d102-threadcpu-name-20261006-064823` (301.9 s, 2560x1440/Mailbox, ShaderValidation + red-zone ON,
no device loss; the first two launches hit the known stale-guest-memory reservation failure, the
third clean).

Measured:
- **`tid=44820 Gpu 0.84-0.89 cores (kernel=14-18%)` is the busiest thread in every interval**;
  seven unnamed guest workers ~0.22-0.32 each; total 2.5-3.1 cores. The role now resolves, and the
  low kernel share means the GPU command thread is **user-space compute-bound**, not blocked in
  syscalls/spins/locks. Per-draw command-thread work is the lever (Dev 101's ambiguity resolved).
- Frame-counter windows: 120-150 s **18.16**, 150-180 s **23.60**, 180-210 s **27.85** (fight);
  60-90 s **8.02** (intro/entrance trough); 240-300 s **6.0** (post-fight/menu). Median 20 / min 1,
  RSS 11,143 MB, background capture dark (display off), no device loss.
- GPU waits by 300 s: `cp_read_fault=4369/51094.0 ms`, `other_threads=6216/59004.7 ms`; both grow
  almost entirely in the last 30 s (+22.0 s / +23.1 s), i.e. the post-fight/menu readback phase
  (Task C), not the fight.
- Fresh fight Tracy (30 s at 120-150 s): `CommandProcessor::Process` 28.38 s/30 s = **94.6% busy**;
  leaves — `SrtRefreshFlatBuffer` **7.03 s (24.8%)**, `PrepareBda`/`SynchronizeBuffersInRanges`
  4.53 s (16.0%), `Image::~Image`+`DeleteImage` 3.26 s (11.5%; 16,047 destroys, ~100-119 us each),
  `CpOpDispatchDirect` 2.81 s (9.9%), `MasterSemaphore::Wait` 2.79 s (9.8%). SRT memo hit only
  2.3% (287,441 / 12,474,904).

Retain: diagnostic only, default off. No FPS gain claimed; 25/30 FPS unmet (best window 27.85).
No commit/push.

Next hypothesis: implement the plan's shadow-verified cross-draw reuse for `SrtRefreshFlatBuffer`
(the #1 leaf, 24.8%), and/or reduce the 11.5% image create/destroy churn. Do not retry BDA range
bookkeeping (Dev 100 neutral).

### UFC Dev 101 — opt-in per-thread CPU sampler shows the frame rate is latency/serialization-bound, not throughput-bound (2026-10-06, 06:3x)

Cycle goal (overnight plan §3): the aggregate Tracy zone profile is flat and repeated per-draw CPU
optimizations (BDA merge, metadata memo, indirect) produced no FPS gain, so identify the actual
frame-rate-limiting host thread before spending another cycle on per-draw work.

Engine change (opt-in, default OFF): `KYTY_UFC_THREAD_CPU=1` (AutoFight label `threadcpu`). A
sampler thread every 30 s enumerates the process threads (`CreateToolhelp32Snapshot` +
`GetThreadTimes`) and prints each thread's CPU as busy cores over the interval; `SetCurrentThreadName`
names the Gpu/Present/Main host threads. Files: `src/common/threads.cpp`, `AutoFight.ps1`.

Verification: build Dev 101 OK; full CTest **54/54**. Fight
`d101-threadcpu-20261006-062645` (421.7 s, 2560x1440/Mailbox, ShaderValidation+red-zone ON, no Tracy,
no device loss; the first launch failed to reserve guest address space = the known stale-memory
hazard, reran clean).

Measured (14 intervals, frame-counter windows from `metrics.csv`):
- **Total process CPU 2.5-3.2 cores; the busiest single thread is only 0.84-0.90 cores**, seven
  workers ~0.25-0.33 each. **No thread is CPU-saturated.**
- Frame windows: 100-150 s **14.54**, 150-250 s **19.73**, 250-300 s **24.62** fps. GPU utilization
  in those windows: median 38 / 44 / 53% (max 60%). Process CPU ~2.8-2.9 cores throughout.
- Therefore, even in the best gameplay window **neither the CPU nor the GPU is saturated**: the FPS
  is limited by serialized dependencies/waits, not by raw per-draw CPU work. This explains why the
  earlier per-draw CPU optimizations did not move FPS.
- GPU-wait counters over the same run: `other_threads` and `cp_read_fault` dominate the command
  thread's blocking time (~25.1 s and ~17.4 s cumulated by 420 s), i.e. cross-thread and host-read
  synchronization, consistent with the serialization finding.
- Limitation: thread names did not resolve via `GetThreadDescription` (all top tids printed `-`), so
  the identity of the 0.88-core leading thread is not yet proven (the GPU command thread is the
  expected candidate); the naming must be fixed to confirm.

Retain: diagnostic only, default off. No FPS gain claimed; no commit/push.

Next hypothesis: (a) fix thread naming with an in-process tid->name registry and re-run to identify
the leading thread; (b) attack **serialization** rather than per-draw throughput: the command lane's
`SendCommandSync` round-trips and the `other_threads`/`cp_read_fault` waits (each host read/write of
GPU-owned memory blocks the command processor until the GPU queue is idle). Target those sync points
before any further per-draw CPU work.

### UFC Dev 100 — media/intro framedump acceptance run: intro/menu/gameplay render correctly; one transient all-black presented frame at a scene cut; intro trough 8.2 fps, sustained 21.8 fps (2026-10-06, 06:16)

Cycle goal (overnight plan §2, the immediate mandate "working media/intro playback"): make the
intro/media window **visible** while the monitor output is off, confirm playback renders, measure
its frame pacing, and look for the reported on-load blackout. Previous intro capture
(`d094-load-media-diag`, 02:24) was blind (display off), so it could not settle playback.

No engine change this cycle. Used the Dev 099 opt-in `FrameDumper` (`KYTY_UFC_FRAME_DUMP`, label
`*framedump*`) through the runner:
`d095-load-media-framedump-20261006-061041`, 300.7 s, 2560x1440/Mailbox, ShaderValidation+red-zone
ON, no device loss, breadcrumbs complete (`gpu_started == gpu_completed == 22036426`), no
FATAL/validation errors.

Measured (engine-presented frames, source 3840x2160 -> 960x540, 131 frames, ~2 s cadence):
- Visual truth: EA Sports UFC 5 splash (frame 2), main menu with fighter model and FIGHT NOW /
  CAREER / ONLINE (frame 12), T-Mobile Arena + fighter walk-in corridor (frames 24-28), fighters and
  referee in the cage (frames 32-34), full gameplay ROUND 1 with health/stamina HUD, Makhachev vs
  Della Maddalena (frame 36 onward, still correct at frame 130). **Intro/menu/gameplay all render
  correctly** — the media/intro path plays, consistent with Dev 099.
- Transient black presented frames: frames 8-10 (~17-21 s) are the normal splash->menu load; frames
  24-25 (~49-51 s) are a genuine but *cinematic-dim* arena entrance (mean ~3/2.2, not black); **frame
  30 (~61 s) is a fully black presented frame (mean 0.0) at the corridor->arena cut**. One all-black
  presented frame at a scene transition matches the reported "blackout" class, but it is a single
  frame, not a sustained black scene.
- Frame-counter windows (`frame` column, valid display-off): 50-90 s **8.23**, 100-150 s **15.19**,
  150-250 s **21.77**, 250-300 s **16.25**; median 17; peak ~27 fps at 217-239 s. The 8.2 fps trough
  coincides with the entrance/arena cut (walk-in + scene switch), matching the known ~53 s collapse.
- Intro 45-105 s Tracy (fresh `tracy-zones.csv`): `CommandProcessor::Process` 53.4%, `DrawIndex`/
  `CpOpDrawIndex` 33.5%, `ExecutePreparedDraw` 17.9%, `PrepareDrawRenderState` 15.4%,
  `RefreshShaders` 13.0%, `PrepareGraphicsBindings` 12.2%, **`Draw::PrepareBda`/
  `SynchronizeBuffersInRanges` 9.7% (138,812 calls, mean 74 us)**, `SrtRefreshFlatBuffer` 9.5%,
  `CpOpDispatchDirect` 9.2% (single one-off max **4.69 s**), `MasterSemaphore::Wait` 6.3%. BDA sync
  is the largest self-contained intro cost; its mean (74 us) shows the dirty-log fast path is doing
  real upload work (not bookkeeping), consistent with Dev 100's negative result.
- Reject/retain: no engine change; no FPS gain claimed. 25/30 FPS unmet (best sustained 5-min
  window 21.8, peak 27). No commit/push.

Next hypothesis: (a) the single all-black presented frame at a scene cut (frame 30, ~61 s) — bound
whether the first presented frame after a render-target/scene switch can be a cleared/unprepared
image (guest frame not yet rendered) by dumping the frames immediately around a known cut at 1 s
cadence; (b) the 4.69 s one-off `CpOpDispatchDirect` intro stall — capture the guest dispatch
packet/CS hash at that stall. Keep BDA-sync/SRT per-draw work as the sustained-FPS target.

### CURRENT: UFC Dev 100 — BDA fast-path range merge implemented/verified (54/54); 2v2 A/B shows no FPS benefit, kept opt-in default-off (2026-10-06, 06:05)

Cycle goal (overnight plan §3 per-draw work, evidence-first): attack the largest self-contained
per-draw cost measured in the fresh Dev 099 30 s fight Tracy: `Draw::PrepareBda` /
`BufferCache::SynchronizeBuffersInRanges` = **3.11%** (51,777 calls, mean 91.7 us) and **6.92%** in
the 45–105 s early-load/intro capture. The full-walk path already retains a `m_buffers` cursor "instead
of starting a tree search for every range"; the dirty-log fast path did not — it called
`SynchronizeBuffersInRanges` once per range (one `upper_bound` tree search each).

Engine change (opt-in, default OFF): `KYTY_UFC_BDA_SYNC_MERGE` (AutoFight label `bdamerge`). The fast
path clips the CPU-dirty + newly-registered ranges, sorts/coalesces them (`CoalesceRanges` in
`rangeSet.h`), then walks `m_buffers` once with a retained cursor. `SynchronizeBuffer` never mutates
`m_buffers`, so the cursor is stable; coalescing cannot drop a range and `ForEachUploadRange` still
uploads only CPU-dirty pages, so widened gaps upload nothing clean. Files: `bufferCache.{h,cpp}`,
`rangeSet.h`, `AutoFight.ps1`, `CMakeLists.txt`, `tests/MemoryTrackerTests.cpp`.

Verification:
- Focused: `memory_tracker` passed (new `TestCoalesceRanges`: sort/overlap/adjacent/nested/empty);
  `buffer_cache_ranges` passed; **new `buffer_cache_ranges_merge` (test 54) runs the existing
  native-readback BDA fixture with `KYTY_UFC_BDA_SYNC_MERGE=1`** and passed — real `vkCmdCopyBuffer`
  readback proves the merged walk uploads exactly the same ranges.
- Full CTest **54/54** (was 53/53; the new test is the merge variant).
- 2v2 A/B, 2560x1440/Mailbox, ShaderValidation+red-zone ON, no Tracy, 301 s each, no device loss:
  sessions `d100-bdamerge-a-20261006-054454` (median/min 17/0), `d100-bdamerge-b-20261006-055602`
  (20/1), `d100-bdabase-a-20261006-055030` (21/1), `d100-bdabase-b-20261006-060138` (18/1);
  fight detection is blind with the display off (all screenshots dark), so `fight_frame_rate`/`fight_frames`
  are n/a — use the frame-counter windows below.
  frame-counter windows 100–150 s merge **16.13 / 15.92** (mean 16.03) vs base **16.45 / 17.60**
  (17.03); 150–250 s merge **21.99 / 22.22** (22.11) vs base **21.86 / 18.13** (20.00); intro
  50–90 s merge **7.76 / 8.06** vs base **7.63 / 8.26** (equal). RSS ~11.0 GB. Run-to-run workload
  phase variance (base-b 150–250 = 18.13) is larger than any difference.

Retain/reject: **no measurable FPS benefit; keep opt-in default-off** (same precedent as the Dev 095
metadata-clear memo). No regression; no device loss. The negative result falsifies the hypothesis that
the BDA-sync cost is dominated by per-range tree searches — it is actual upload/metadata work.
No FPS gain claimed; 25/30 FPS unmet (best sustained window ~22–23). No commit/push.

Next hypothesis: the fresh early-load Tracy (`d094-load-media-diag-20261006-022414/tracy-zones.csv`,
45–105 s) shows the intro is command-thread-bound (`CommandProcessor::Process` 53.4%, `DrawIndex` 33.0%,
`RefreshShaders` 14.5%, `MaterializeResources` ~13%) **plus** large one-off stalls (single
`CpOpDispatchDirect` max 4.69 s, `PrepareBda` max 17.67 s, `ReadMemory` max 170 ms). Target those
intro stalls or the per-draw SRT materialization next, not the BDA range bookkeeping.

### UFC Dev 099 — opt-in engine presented-frame dump restores visual truth display-off; intro/menu/gameplay render correctly (2026-10-06, 05:10)

Cycle goal (overnight plan §2 + the plan's declared next job): make unattended visual verification
work with the monitor output off, so media/intro playback and visual correctness can actually be
judged, and use it to settle the reported on-load blackout.

Engine change (opt-in, default OFF): `Presenter::Impl::Present` gains a `FrameDumper`
(`KYTY_UFC_FRAME_DUMP=<dir>`). It records one `vkCmdCopyImageToBuffer` of the prepared guest frame
into a host-visible/coherent buffer **inside the existing present command buffer**, then polls the
present scheduler timeline later (`IsFree`) and encodes a downscaled PNG — deliberately **no
in-present wait**, which is what hung the Dev 096 attempt. Supports the 8-bit and the game's 10-bit
presented formats (`A2R10G10B10`/`A2B10G10R10_UNORM_PACK32`, value 58). `AutoFight.ps1` gains a
`*framedump*` label hook. Files: `src/graphics/presentation/window/swapchain.cpp`, `AutoFight.ps1`.

Measured (Dev 099):
- Full CTest **53/53** (run after both the Dev 098 and Dev 099 edits).
- `d099-framedump-diag2` (301.12 s): **132 frames** written, source 3840x2160 -> 960x540, median 17–20 FPS,
  frame-counter windows 60–100 s **10.04**, 100–150 s **16.23**, 150–250 s **23.29**, 250–300 s **23.20**,
  RSS 11.1 GB, **no device loss** (all breadcrumbs completed).
- `d099-framedump-diag3` (240.88 s): **104 frames**, 9.35 / 15.08 / 20.72, no device loss.
- Clean default-path control `d099-clean-baseline` (300.95 s, feature OFF): median 17, 9.75 / 17.14 /
  20.75 / 17.4, no `frames/` dir, no device loss — same fast-mode range, no regression from the opt-in path.
- Visual evidence (both dump runs): the game renders the EA Sports UFC 5 splash, the **full animated
  intro cinematic** (helmet/soccer-ball flythrough), the main menu (FIGHT NOW/CAREER/ONLINE...), and
  **full gameplay** (Makhachev vs Della Maddalena with health/stamina HUD, round timer, cage, crowd,
  referee). So intro/menu/gameplay rendering is correct in the automated path and the user's reported
  immediate-on-load black does **not** reproduce there (consistent with the display-off capture finding).

Retain (opt-in, default off; no device loss; visual verification only). No FPS gain claimed; 25/30 FPS
unmet (best sustained window ~23.3). No commit/push. Next: use the dump + frame-counter windows for
per-draw/intro A/B (`RefreshShaders`/`MaterializeResources`, `Draw::PrepareBda`, `cp_read_fault`) and
sample the intro at fine cadence to confirm the cinematic frame pacing.

### CURRENT: UFC Dev 097 — the overnight black is the display output being OFF, not a render regression; metadata-clear memo measured (no gain) (2026-10-06, 04:35)

Cycle goal (overnight plan §1, then §3): settle whether the all-black captures since 02:24 are a renderer
regression; if inconclusive, advance an evidenced measured milestone.

**Correction to the entry below:** the black client is a display-off artifact. `nvidia-smi ... display_active`
is **Disabled** and `powercfg /requests` shows **DISPLAY: none**; with the output off the compositor does not
update, so `CopyFromScreen`, `PrintWindow` and a direct desktop capture all return black while the emulator
keeps presenting. `SetThreadExecutionState(DISPLAY|SYSTEM)`, the monitor-power VK message and synthetic
input all failed to re-enable it (monitor physically off). A "bright" Dev 094 run (01:10) and a "black"
Dev 097 all-off run (03:57) have near-identical frame/RSS trajectories and shader counts, so the guest does
the same work — this is measurement, not rendering. The user's on-load blackout is **not currently
reproducible**, and no Dev 094 default feature is implicated.

Measured (frame counter, valid with the display off): Dev 095 metadata-clear memo A/B 2v2
(`d097-memclear-on-a/-b` vs `d097-memobase-a/-b`, RFC 300 s each): memo ON 16.99/20.66 fps vs OFF
17.27/21.41 fps over 100–150/150–250 s; 60–100 s identical (9.73–10.25). **No benefit** — keep
`KYTY_UFC_META_CLEAR_MEMO` opt-in default-off. No device loss. No engine change, no commit/push; 25/30 FPS
unmet. Next: engine-owned presented-frame output off the present command buffer (dedicated one-shot
submission, no in-present wait), then per-draw/intro A/B on `cp_read_fault`, `Draw::PrepareBda` and
`RefreshShaders`/`SrtRefreshFlatBuffer`.

### UFC Dev 097 — overnight capture was blind (PrintWindow black); fixed with DPI-aware screen capture, blackout now reproducible and not a Dev094-feature regression (2026-10-06)

Cycle goal (overnight plan §1/§2): make overnight visual measurement truthful, then use it to isolate the
immediate-on-load blackout. Only `AutoFight.ps1` changed: keep the display composited for the run
(`SetThreadExecutionState`, released at end), force the capture window visible/topmost/foreground, and replace
the screenshot path with a DPI-aware `CopyFromScreen` of the client region (`-ScreenCapture:$false` restores
PrintWindow). No engine/build/install change; Dev 097 install untouched.

Measured findings:
- `PrintWindow` is the blind path. A live desktop screen capture shows the emulator client is genuinely black
  while its title renders `frame: 2120, fps: 8`. All runs since 02:24 reported constant PrintWindow brightness
  ~10.6; `d094-load-bis-ab` (Dev 094) was black at 02:33 while the identical all-on Dev 094 build showed
  content at 01:12 — capture/environment changed, not the build.
- The DISPLAY request was verified registered and the window `visible=True fg=True`, yet PrintWindow stayed
  black — the "display not composited" explanation is insufficient; `CopyFromScreen` is the working method.
- After the fix the client reads **3.1** (truthful) vs the old constant 10.6.
- Blackout isolation: `d097-bis-abcdefghijklmnop` (all Dev 094 defaults OFF, 151.12 s) = **25/25 dark**, no
  fight. So no Dev 094 bake-off switch causes the black scene; it is on the common path.
- Regression: full CTest **53/53**; automation harness **23/23**. Runs: `d097-displayon-baseline` 361.18 s
  (all dark, median 18), `d097-topmost-capture-test` 121.48 s, plus two rerun-only reservation failures and
  one intermittent `d096`-class TDR.

Retain the AutoFight capture fix; no engine change, no commit/push. No FPS gain; 25/30 FPS unmet. Next:
inspect the guest media/video-decode path, saved-profile/network state, and a fresh-driver control to explain
the stuck black scene (identical build rendered earlier); confirm capture turns bright when content renders.

### CURRENT: UFC Dev 097 — engine presented-frame dump reproduced a GPU hang, reverted; frame-counter FPS is valid with the display off (2026-10-06)

Cycle goal (overnight plan §1/§2, Dev 095 next hypothesis): make overnight visual measurement
independent of the desktop compositor. Implemented an opt-in engine presented-frame dump
(`KYTY_UFC_FRAME_DUMP=<dir>`) in `Presenter::Impl::Present`: copy the prepared guest frame to a
host-visible buffer inside the present command buffer, wait the present tick, write a downscaled
PNG. Build `dev096-frame-dump` OK (UFC Dev 096).

It is a **confirmed regression and was reverted**: `d096-framedump-diag` (32.49 s) and
`d096-framedump-diag-b` (32.34 s) both exited 321 with `gpu_device_lost=True`,
`gpu_loss_class=timeout`, nvlddmkm 153 and identical `DrawIndex` breadcrumbs / `WriteInvalid 0x0`;
two all-black dumps were written in each. Control `d096-control-a` (same binary, feature inactive)
ran **301.09 s clean**, as did the earlier Dev 095 421.69 s run. Reverted only this cycle's edits
(`swapchain.cpp`, `AutoFight.ps1`); rebuilt **UFC Dev 097**, full CTest **53/53**,
`d097-revert-baseline` **301.8 s clean**.

Kept measurement insight: `metrics.csv` `frame` tracks the real renderer even while PrintWindow is
black, so frame-derived FPS is usable display-off. Dev 097 100–150 s **15.26** / 150–250 s **15.37**;
Dev 096 control 15.93 / 18.02. Only FightWatch's screenshot-based start detection is blind.
Nothing retained; no FPS gain claimed; 25/30 FPS unmet; no commit/push. Next: off-thread one-shot
readback (dedicated submission + timeline wait + held frame) with verify mode, or keep the display
composited for overnight captures, then A/B the Dev 095 metadata-clear memo.

### UFC Dev 095 — HTile metadata-clear memo (opt-in); fight captures blind without a composited display (2026-10-06)

Advanced one evidenced engine change from the overnight plan's independent work (blackout investigation
inconclusive, so continued per plan). Build **UFC Dev 095** `dev095-meta-clear-memo` OK. Files:
`src/graphics/host_gpu/renderer/cache/textureCache.{h,cpp}` (new `NoteSurfaceMetaChange`, atomic
`m_surface_meta_generation`, memoized `IsMetaCleared` fast path, verify mode) and `AutoFight.ps1` (`*memoclear*`
/`*memoclearverify*` label hooks). Switch `KYTY_UFC_META_CLEAR_MEMO=1` (default OFF), verify
`KYTY_UFC_META_CLEAR_MEMO_VERIFY=1`. All `m_surface_metas` mutation sites bump the generation; the memo is
GPU-thread-only and default-off, so default rendering is unchanged.

Verification: focused meta-clear/htile tests passed; full CTest **53/53**. Fight
`d095-memoclearverify-20261006-025122` (421.69 s, timeout, no crash): **no memo mismatch over the run** —
memo path correctness validated on a real workload. FightWatch detected no fight because all 68 screenshots
were black (brightness ≤10.6) while the emulator rendered (frame 9737, ~17-24 fps; counters proportionate to
the clean baseline). Cause: `powercfg /requests` DISPLAY: none — the display is not held composited, so
PrintWindow returns a black client area. This also explains the earlier `d094-load-media-diag` black run.

Retain as opt-in, default-off; **no FPS gain claimed** (no valid fight window captured). 25 FPS and 30 FPS
unmet with evidence. No commit/push. Next: emulator-owned frame dump or hold the display on during fights so
overnight measurements are valid, then A/B the memo with alternating controls before any default enablement.

### CURRENT: unattended corrective/media/performance work authorized (2026-10-06)

The user clarified **black immediately on loading into gameplay**, asked to restart the loop
and continue independently while they sleep, and requested media playback plus a measured
25-FPS milestone. They approved starting/recovering project automation, keeping the PC awake,
and diagnostic runs up to 15 minutes. FIRST read `_Build/agent-reports/OVERNIGHT-PLAN-20261006.md`;
it supersedes the old three-run stop, Task B/C deferral and reproduction-input wait below.
Missing blackout details must not halt independent media/intro or evidenced engine work.
The old three all-on runs remain useful control evidence, not proof that blackout is fixed.

Both automation processes had exited when this request was checked; the stale worker heartbeat
was correctly rejected by RunnerJob.py. The current work is starting fresh instances with
worker-owned early-load capture, automatic absent-worker recovery and supervisor retry.
No media/FPS improvement has yet been established by this restart; Dev 094 remains the engine
baseline. Preserve quality, all guest work, retained ISA/BDA fixes and 53/53 tests. All builds,
CTest and game runs still use RunnerJob.py. Sol -> verified OpenRouter DeepSeek fallback stays.
Next concrete diagnostic is `d094-load-media-diag`, early Tracy at 45 s, dense screenshots and
shader dumps. Consume its evidence if already queued/completed instead of launching duplicates.

### CURRENT: UFC Dev 094 — supervisor waiting for reproduction input (2026-10-06, 02:04)

The user reported no visible gameplay. Actual diagnosis: the three prescribed all-on fights
finished, but subsequent model cycles repeatedly documented the same non-reproduction blocker
instead of making progress. No additional gameplay or engine improvement occurred in those
blocked cycles. Preserve their historical entries below; do not repeat them.

The supervisor now recognizes `_Build/agent-loop/WAIT-FOR-INPUT.json` and remains alive with
`status=waiting for user input`, polling every 60 s without launching coding agents or spending
cycles. The running process was reloaded after cycle 53 finished normally; current supervisor
PID 43912, worker heartbeat fresh, STOP absent, no active emulator/launcher/model invocation.
Sol -> OpenRouter DeepSeek V4.1 Flash fallback remains configured. Four focused CPU automation
checks passed (input wait/resume, blocked-cycle no-spend, fallback selection, cooldown wait);
OpenCode agent configuration validated. No engine/C++ build or CTest was needed for this change.

Gameplay reference remains Dev 094: three runs at 13.20 / 18.93 / 20.73 frame-derived FPS,
~199 s fighting each, no captured black scene or device loss. These varying workloads do not
prove a performance improvement. Task A still needs blackout timing, first/later match in
one emulator process and mode/fighters, ideally a live-HUD black screenshot. After recording
new user details/steering in the plan, clear WAIT-FOR-INPUT.json to resume the existing loop.
The bounded Task A rules still apply; no negative-only bisect or speculative feature rollback.

### CURRENT: UFC Dev 094 — Task A stop condition still applies (2026-10-06, 02:02)

Read NEXT-STEPS-DEV094 first, this PC's transfer paths and required review sections.
Verified the original three-baseline-limit evidence in LOOP-LOG: no captured blackout;
no new triggering scenario supplied. No fourth baseline or negative-only bisect queued.
Ownership: no UFC launcher/emulator or competing build/test/game automation observed;
caller KYTY_UFC_* empty. Heartbeat 02:02:39.637 was ~2 s old at clock snapshot.
Installed Dev 094/branch dev/all pre-existing work preserved. Only HANDOFF.md and
LOOP-LOG.md edited; no engine/default change, build, focused/full tests or new session.
Historical 53/53 and saved three-session scenario/FPS/resource/wait evidence unchanged
(see new LOOP-LOG entry). No candidate retained/rejected, feature cleared or gain claimed.
30 FPS unmet; manual transient-artifact check missing. Task B/C deferred.
Blocker: actual blackout timing, first/later match in one process and mode/fighters are
unknown; need those details, ideally a black-scene screenshot with live HUD. Longer or
later-match triggering remains unproven. Resume bis-letters after positive reproduction,
two clean runs per clean verdict. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — blocked on the actual blackout scenario (2026-10-06, 02:01)

Read the overriding NEXT-STEPS plan, transfer paths and required review sections;
verified the three-baseline-limit entry in LOOP-LOG. Three all-on attempts already
captured no blackout; no new trigger supplied. Honor the prescribed stop: no fourth
baseline, negative-only bisect, speculative default change or Task B/C work.
Ownership snapshot: no UFC launcher/emulator or competing build/test/game automation;
caller KYTY_UFC_* empty. Heartbeat content 02:01:30 was ~10 s before clock snapshot;
no runner job queued. Branch dev/installed Dev 094/existing work preserved.
Files changed: HANDOFF.md and LOOP-LOG.md only. No build, focused/full tests or new
session; historical 53/53 and scenario/FPS/resource/wait metrics remain the saved
three-baseline evidence. No candidate retained/rejected or gain claimed; manual
transient-artifact check missing, 30 FPS unmet. Required next evidence: time into
fighting when black appeared, first/later match in the same process, mode/fighters,
ideally a screenshot with black 3D scene and live HUD. Longer/later-match triggering
is unproven. Resume bis-letters after positive reproduction, two clean runs per clean
verdict. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — Task A remains blocked by non-reproduction (2026-10-06, 02:00)

Verified NEXT-STEPS-DEV094 first, transfer paths, ufc-loop and required review sections.
The saved three all-on attempts exhaust the prescribed reproduction limit; this request
supplies no new blackout trigger. No fourth baseline, negative-only bisect or speculative
engine/default change. Pre-flight: no UFC launcher/emulator or competing build/test/game
automation observed; caller KYTY_UFC_* empty. Runner heartbeat 02:00:21 was ~2.2 s old
at the clock snapshot; no runner job needed. Dev 094/branch dev/existing work preserved.
Files changed: HANDOFF.md and LOOP-LOG.md only. No build, focused/full tests or gameplay;
historical 53/53 unchanged. Prior sessions, scenario, first_fight_s, frame rates/frames,
median/min, RSS/VRAM, wait-site ms/s and visuals remain the three-baseline LOOP-LOG
reference. No candidate retained/rejected or speedup claimed; manual transient-artifact
check missing, 30 FPS unmet. Task B/C deferred. Required next evidence: actual time into
fighting when black appeared, first/later match in the same process, mode/fighters,
ideally a black-scene screenshot with live HUD. Longer/later-match triggering remains
unproven. Resume prescribed bis-letters only after positive reproduction, two clean runs
per clean verdict. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — Task A reproduction blocker unchanged (2026-10-06, 01:59)

Read overriding NEXT-STEPS plan, transfer settings and required review sections.
Runner availability job `oc20261006-015906-gitstatus` exited **0**. Ownership snapshot:
no UFC launcher/emulator or competing build/test/game automation; caller KYTY_UFC_*
empty (EpicGamesLauncher is not the UFC launcher). Branch dev and existing work preserved.
The prescribed three all-on attempts already captured no blackout; no new triggering
scenario was supplied. No fourth baseline, negative-only bisect or speculative default
change. Only HANDOFF.md/LOOP-LOG.md edited; no engine change/build/focused or full tests/
new gameplay session. Historical Dev 094 **53/53** and three-baseline session/scenario,
first_fight_s, fight_frame_rate/frames, median/min, RSS/VRAM and wait-site ms/s remain
unchanged (LOOP-LOG three-baseline-limit entry). No candidate accepted/rejected or
performance gain claimed; manual transient-artifact check missing; **30 FPS unmet**.
Task B/C deferred. Blocker: actual blackout timing, first/later match in one process,
mode/fighters and ideally a black-scene screenshot with live HUD are still missing.
Longer/later-match trigger is unproven. Resume bis-letters only after positive
reproduction, two clean runs per clean verdict. Runner/supervisor left untouched.

### CURRENT: UFC Dev 094 — Task A non-reproduction stop verified (2026-10-06, 01:58)

Read overriding plan, transfer paths and required review sections. Runner availability
job `oc20261006-015757-gitstatus` exited 0. No UFC launcher/emulator or competing
build/test observed; caller KYTY_UFC_* empty. Three saved all-on attempts already
exhaust Task A's reproduction limit with no captured blackout; no new trigger supplied.
No engine/default change, build, focused/full tests or new gameplay session. Installed
Dev 094/branch dev/all existing work preserved; only HANDOFF.md/LOOP-LOG.md edited.
Historical 53/53 and saved session/scenario, first_fight_s, frame rate/frames,
median/min FPS, RSS/VRAM and wait-site ms/s unchanged (three-baseline LOOP-LOG entry).
No candidate retained/rejected, feature cleared or gain claimed; manual transient
check missing, 30 FPS unmet. Task B/C remain deferred. Blocker: actual blackout time
into fighting, first/later match in one process and mode/fighters unknown; need those
details, ideally a black-scene screenshot with live HUD. Longer/later-match triggering
is unproven. Resume bis-letters halving only after positive reproduction, two clean
runs per clean verdict. No redundant baseline or speculative default change.

### CURRENT: UFC Dev 094 — Task A needs reproduction details (2026-10-06, 01:56)

Read NEXT-STEPS-DEV094 first, transfer paths and required review sections; verified the
saved three-baseline decision in LOOP-LOG. All three prescribed all-on runs captured no
blackout, so the plan's stop condition remains binding. No new trigger was supplied.
Ownership snapshot: no UFC launcher/emulator or competing build/test observed; caller
KYTY_UFC_* empty. Heartbeat content 01:56:47 was ~1.5 s before the clock snapshot.
Branch dev/installed Dev 094/all existing uncommitted work preserved. Files edited:
HANDOFF.md and LOOP-LOG.md only. No runner job, build, focused/full tests or new session;
historical 53/53 and saved first_fight_s, frame rate/frames, median/min, RSS/VRAM,
wait-site ms/s and visual evidence unchanged. No engine candidate retained/rejected,
feature cleared or performance improvement claimed; manual transient check missing,
30 FPS unmet. Task B/C deferred. Need actual blackout time into fighting, first/later
match in one process, mode/fighters, ideally a live-HUD black screenshot. Longer/later
match triggering remains unproven. Resume bis-letters halving after positive reproduction,
two clean runs per clean verdict; no redundant baseline or speculative default change.

### CURRENT: UFC Dev 094 — Task A bounded stop; trigger still missing (2026-10-06, 01:55)

Read the overriding NEXT-STEPS plan first, transfer paths, ufc-loop and required review
sections. Verified the saved three-baseline decision: no captured blackout, reproduction
limit exhausted; this invocation supplies no new triggering scenario. No redundant fight,
negative-only bisect or speculative default change. Ownership checks found no UFC launcher,
emulator or competing build/test; caller KYTY_UFC_* empty. Heartbeat content was 01:55:43,
four seconds before the clock check (no runner job needed or queued).
Branch dev, installed Dev 094 and all pre-existing engine changes/defaults preserved.
Files changed: HANDOFF.md/LOOP-LOG.md only. Build/focused/full tests: not run; historical
53/53 unchanged. No new session, first_fight_s, fight_frame_rate/frames, median/min FPS,
RSS/VRAM, wait-site ms/s or visual evidence; saved three-baseline measurements remain reference.
Decision: no engine candidate retained/rejected, no feature cleared or speedup claimed;
manual transient check missing, 30 FPS unmet. Task B/C deferred under the current plan.
Blocker/next evidence: blackout time into fighting, first/later match in one process,
mode/fighters, ideally a black-scene screenshot with live HUD. Longer/later-match triggering
remains a hypothesis. Resume bis-letters halving after positive reproduction, two clean
runs per clean verdict. Runner/supervisor untouched; no commit/push or process termination.

### CURRENT: UFC Dev 094 — Task A reproduction input required (2026-10-06, 01:54)

Verified NEXT-STEPS-DEV094 first, this PC's transfer paths, required review sections and the
saved three-baseline decision. Runner availability **oc20261006-015439-gitstatus**, **exit 0**;
no UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty.
The prescribed three all-on attempts have no captured blackout; no new triggering scenario
was provided. Stop condition remains binding: no redundant fight, negative-only bisect,
speculative default change or Task B/C work. Dev 094/branch dev/all existing work preserved.
Files edited: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or new session;
historical **53/53**, first_fight_s/frame rates/frames, median/min, RSS/VRAM, wait-site ms/s
and visual evidence remain in the three-baseline LOOP-LOG entry. No candidate decision or
gain claimed; manual transient check missing, **30 FPS unmet**. Next required evidence:
blackout time into fighting, first/later match in one process, mode/fighters, ideally a
black-scene screenshot with live HUD. Longer/later-match triggering is unproven. Resume
bis-letters halving after positive reproduction, two clean runs per clean verdict.

### CURRENT: UFC Dev 094 — Task A still blocked by non-reproduction (2026-10-06, 01:53)

Current plan/transfer paths and required review sections read. Runner availability job
**oc20261006-015318-gitstatus**, **exit 0**; no UFC launcher/emulator or competing build/test
observed, caller KYTY_UFC_* empty. The three saved all-on baselines already exhaust the
plan's reproduction limit without captured blackout; no new triggering scenario supplied.
No engine/default changes, build, focused/full tests or fight this invocation. Existing
Dev 094/branch dev/uncommitted work preserved; only HANDOFF.md and LOOP-LOG.md edited.
Historical **53/53**, session metrics and wait-site evidence remain unchanged (three-baseline
LOOP-LOG entry); no new first_fight_s, frame rate/frames, median/min, RSS/VRAM or visuals.
No candidate retained/rejected or gain claimed; manual transient check missing, **30 FPS unmet**.
Next required input: approximate blackout time into fighting, first/later match in the same
process, mode/fighters and ideally a live-HUD black screenshot. Longer/later-match triggering
is only a hypothesis. Resume prescribed bis-letters halving after positive reproduction,
two clean runs per clean verdict; Task B/C deferred. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — prescribed non-reproduction stop honored (2026-10-06, 01:52)

Read NEXT-STEPS-DEV094 first, transfer paths, ufc-loop and required review/history sections.
Existing three all-on sessions below exhaust Task A's reproduction attempts; no new blackout
trigger supplied. Runner availability job **oc20261006-015211-gitstatus**, **exit 0**.
No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty.
Preserved branch dev, installed Dev 094, experimental defaults and all uncommitted engine work.
Only HANDOFF.md/LOOP-LOG.md updated; no build, focused/full tests or fight. Historical **53/53**
unchanged. Session/scenario, first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM,
wait-site ms/s and visuals: no new measurements; saved three-run evidence below remains reference.
No candidate retained/rejected or speedup claimed; manual transient check missing, **30 FPS unmet**.
Blocker/next hypothesis: blackout requires an unrecorded scenario, possibly longer/later-match
play. Need approximate time into fighting, first/later match and mode/fighters, ideally a
live-HUD black screenshot. Resume bis-letters halving after positive reproduction, two clean
runs per clean verdict. Task B/C deferred; no runner/supervisor changes, commit or push.

### CURRENT: UFC Dev 094 — Task A blocked; saved summaries verified (2026-10-06, 01:51)

Current plan and transfer paths read first; required ufc-loop/history/review sections read.
Verified the three saved all-on summaries (`d094-all-20261006-011057`, `...-012113`,
`...-012712`): first_fight_s **102.00/101.89/101.92**, frames **2601/3741/4095**,
fight_frame_rate **13.20/18.93/20.73**, fight median/min **14/2, 19/7, 22/12**,
RSS **15303.3/11111.8/11017.7 MiB**, device VRAM **15062/13321/12503 MiB**.
These are prior runs, not new measurements. Existing screenshot review/scan found no
captured blackout; manual transient-artifact check remains missing. Prior wait-site ms/s
and opening-window evidence remain in the three-baseline LOOP-LOG entry. The plan's
three-run non-reproduction stop applies; no negative-only bisect or speculative patch.
Runner availability job **oc20261006-015059-gitstatus**, **exit 0**; no UFC launcher/emulator
or competing build/test observed, caller KYTY_UFC_* empty. Branch dev/installed Dev 094
and all existing uncommitted engine work/defaults preserved. Only HANDOFF.md/LOOP-LOG.md
edited; no build, focused/full tests or fight. Supplied **53/53** remains historical.
No candidate retained/rejected or performance gain claimed; **30 FPS unmet**, Task B/C
deferred. Blocker/next hypothesis: need the actual blackout trigger (time into fighting,
first/later match, mode/fighters, ideally a live-HUD black screenshot); longer/repeated
matches remain unproven. Resume prescribed halving after positive reproduction, two clean
runs per clean verdict. Runner/supervisor untouched; no commit/push.

### CURRENT: UFC Dev 094 — Task A stop condition unchanged (2026-10-06, 01:48)

Read current plan first, transfer paths, ufc-loop and required review/history sections.
Recorded all-on sessions `d094-all-20261006-011057`, `...-012113`, `...-012712`
already exhaust the three-run reproduction limit without captured blackout. This request
adds no trigger; do not reset the count or infer a culprit from negative-only evidence.
Runner availability job **oc20261006-014804-gitstatus**, **exit 0**. No UFC launcher/emulator
or competing build/test observed; caller KYTY_UFC_* empty. Branch dev/installed Dev 094,
defaults and all pre-existing engine work preserved. Files changed: HANDOFF.md/LOOP-LOG.md
only. No build, focused/full tests or fight; supplied **53/53** remains historical. No new
session/first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM, wait-site ms/s
or visuals. No candidate retained/rejected or gain claimed; manual transient check missing,
**30 FPS unmet**. Task B/C deferred. Blocker/next hypothesis: actual blackout timing,
first/later match and mode/fighters are missing; longer/repeated-match trigger unproven.
Need that scenario, ideally a black-scene screenshot with live HUD, before positive
reproduction and prescribed halving (two clean runs per clean verdict). Runner/supervisor
untouched; no commit/push.

### CURRENT: UFC Dev 094 — Task A bounded reproduction stop still applies (2026-10-06, 01:47)

Read NEXT-STEPS-DEV094 first, transfer settings and required current/history sections.
Verified the existing three all-on sessions: no captured black-scene failure; no new
trigger supplied. Do not reset that count or infer a culprit from negative-only bisects.
Runner availability: `py -3.13 RunnerJob.py gitstatus`, **oc20261006-014700-gitstatus**,
**exit 0**. No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_*
empty. Branch dev/installed Dev 094/defaults and all pre-existing work preserved.
Only HANDOFF.md/LOOP-LOG.md edited; no build, focused/full tests or fight. Latest supplied
**53/53** is historical. No new session/first_fight_s, fight_frame_rate/frames, median/min,
RSS/VRAM, wait-site ms/s or visual evidence; three-run reference remains below. No engine
candidate retained/rejected or performance gain claimed. Manual transient check missing;
**30 FPS unmet**. Task B/C deferred. Blocker/next hypothesis: blackout may require longer
or repeated-match play; need actual time into fighting, first/later match, mode/fighters,
ideally a screenshot showing black scene plus live HUD. Resume prescribed halving after
positive reproduction, two clean runs per clean verdict. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — reproduction input still missing (2026-10-06, 01:46)

NEXT-STEPS-DEV094 read first; transfer paths and required instructions verified. Task A's
three all-on baselines are already exhausted without a captured blackout. No new trigger
was supplied, so no repeated baseline, negative-only bisect or speculative engine change.
Runner availability: `py -3.13 RunnerJob.py gitstatus`, **oc20261006-014551-gitstatus**,
**exit 0**. No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_*
empty. Branch dev, installed Dev 094, defaults and existing uncommitted work preserved.
Files edited: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or fight; latest
supplied **53/53** is historical. No new session/first_fight_s, fight_frame_rate/frames,
median/min FPS, RSS/VRAM, wait-site ms/s or visuals. No candidate accepted/rejected or
performance improvement claimed; manual transient-artifact check missing, **30 FPS unmet**.
Task B/C deferred. Blocker/next hypothesis: blackout may require a longer or later match;
need actual time into fighting, first/later match in the same process, mode/fighters and
ideally a black-scene screenshot with live HUD. Resume prescribed feature halving only
after positive reproduction, two clean runs per clean verdict. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — Task A still needs the actual blackout scenario (2026-10-06, 01:44)

Current plan/transfer paths and required instructions verified. Its three all-on baseline
attempts remain exhausted without a captured blackout; this request supplies no new trigger.
Runner availability job **oc20261006-014436-gitstatus**, exit **0**. No UFC launcher/emulator
or competing build/test observed; caller KYTY_UFC_* empty. Branch dev, installed Dev 094,
all defaults and pre-existing engine work preserved. Only HANDOFF.md/LOOP-LOG.md edited.
No build, focused/full tests or fight; latest supplied **53/53** is historical. Session,
first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM, wait-site ms/s and visuals:
no new measurements (three-run reference below). No candidate retained/rejected or speedup
claimed; manual transient-artifact check missing, **30 FPS unmet**. Task B/C deferred.
Blocker/next hypothesis: longer or repeated-match triggering remains unproven. Need actual
time into the fight, first/later match in the same process, mode/fighters, ideally a black-scene
screenshot with live HUD. Resume bis-letters halving only after positive reproduction, two
clean runs per clean verdict; do not reset the three-run count. Runner/supervisor untouched.

### CURRENT: UFC Dev 094 — Task A reproduction blocker confirmed (2026-10-06, 01:43)

Read NEXT-STEPS-DEV094 first, transfer paths and required handoff/review sections.
No additional blackout scenario was supplied. The three recorded all-on baselines
already exhaust the plan's reproduction limit; do not reset it or infer a culprit
from negative-only runs. Runner availability check `py -3.13 RunnerJob.py gitstatus`,
**oc20261006-014316-gitstatus**, exited **0**. No UFC launcher/emulator or competing
build/test observed; caller KYTY_UFC_* empty (EpicGamesLauncher is unrelated).
Installed **Dev 094**, branch dev and all pre-existing engine work/defaults preserved.
Files changed this invocation: HANDOFF.md/LOOP-LOG.md only. No build, focused/full
CTest or fight queued; supplied **53/53** is historical, not new verification.
No new session, first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM,
wait-site ms/s or visual observations. No candidate retained/rejected or gain claimed.
Manual transient-artifact check missing; **30 FPS unmet**. Task B/C remain deferred.
Concrete blocker/next hypothesis: actual blackout timing, first/later match and
mode/fighters are missing; longer/repeated-match triggering remains unproven. Need
that reproduction information, ideally a live-HUD black screenshot, before resuming
`d094-bis-abcdefgh` and halving (two clean runs per clean verdict). No runner/supervisor
changes, foreign-process termination, commit or push.

### CURRENT: UFC Dev 094 — Task A reproduction blocker (2026-10-06, 01:42)

Current plan/transfer paths and required sections verified. Three all-on baselines already
exhausted the prescribed reproduction limit; this request adds no blackout scenario.
Runner `py -3.13 RunnerJob.py gitstatus`: **oc20261006-014211-gitstatus**, **exit 0**.
No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty.
Dev 094/branch dev, defaults and existing uncommitted engine work preserved. Only
HANDOFF.md/LOOP-LOG.md changed. No build, focused/full tests or fight; supplied **53/53**
is historical. No new session/first_fight_s, fight_frame_rate/frames, median/min FPS,
RSS/VRAM, wait-site ms/s or visuals; no candidate retained/rejected or speedup claimed.
Manual transient-artifact check missing; **30 FPS unmet**. Task B/C deferred. Need actual
blackout timing, first/later match and mode/fighters, ideally a live-HUD black screenshot;
longer/repeated-match trigger remains unproven. Resume prescribed halving after positive
reproduction, two clean runs per clean verdict; do not reset the three-run count.
Runner/supervisor untouched; no commit/push.

### CURRENT: UFC Dev 094 — Task A awaiting reproduction details (2026-10-06, 01:41)

Current plan and transfer paths verified; its three-all-on-run stop condition remains
reached. No new blackout scenario in the transfer request. Runner availability check
`py -3.13 RunnerJob.py gitstatus`, **oc20261006-014056-gitstatus**, **exit 0**.
No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty
(EpicGamesLauncher is unrelated). Branch dev, installed Dev 094 and all existing
uncommitted engine work/defaults preserved. Only HANDOFF.md/LOOP-LOG.md updated.
No build, focused/full tests or fight; supplied **53/53** remains historical. No new
session/first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM, wait-site
ms/s or visual evidence. No candidate retained/rejected or gain claimed; manual
transient-artifact check missing, **30 FPS unmet**. Task B/C remain deferred.
Blocker: need approximate blackout time, first/later match in the same process,
mode/fighters, ideally a black-scene screenshot with live HUD. Do not reset the
three-run count; resume bis-letters halving after positive reproduction, two clean
runs per clean verdict. Runner/supervisor unchanged; no commit/push.

### CURRENT: UFC Dev 094 — reproduction blocker unchanged (2026-10-06, 01:40)

NEXT-STEPS-DEV094 and transfer paths reread. No new blackout scenario supplied; the
three all-on runs below exhausted Task A's reproduction limit. No repeated baseline,
negative-only bisect or speculative engine/default change. Task B/C remain deferred.
No launcher/emulator or competing build/test observed; caller KYTY_UFC_* overrides empty.
Runner `py -3.13 RunnerJob.py gitstatus`: **oc20261006-013952-gitstatus**, **exit 0**.
Branch dev, installed **Dev 094**, existing uncommitted engine work/defaults preserved.
Only HANDOFF.md and LOOP-LOG.md edited; no build, focused/full tests or fight queued.
Latest supplied **53/53** is historical; no new session/first_fight_s/FPS/frames,
median/min, RSS/VRAM, wait-site ms/s or visual measurements. No candidate accepted/rejected,
no performance gain claimed; manual transient-artifact check missing, **30 FPS unmet**.
Blocker/next hypothesis: actual blackout timing, first/later match and mode/fighters
(ideally a live-HUD black screenshot) are missing; longer/repeated-match trigger unproven.
Resume the prescribed bisect after positive reproduction, two clean runs per clean verdict.
Runner/supervisor untouched; no commit or push.

### CURRENT: UFC Dev 094 — Task A remains blocked (2026-10-06, 01:38)

Current plan and transfer paths read; no new blackout reproduction scenario was provided.
The three `d094-all` attempts below already meet the prescribed non-reproduction stop
condition. Do not restart them or infer a guilty feature from negative-only evidence.
Runner availability confirmed with `py -3.13 RunnerJob.py gitstatus`, job
`oc20261006-013838-gitstatus`, **exit 0**. No UFC launcher/emulator or competing build/test
observed; caller KYTY_UFC_* overrides empty. No build, CTest or fight queued.

Installed **Dev 094**, branch dev, engine defaults and pre-existing uncommitted work
preserved. Files edited: HANDOFF.md and LOOP-LOG.md only. Focused/full tests not rerun;
latest supplied **53/53** is historical evidence, not verification this invocation.
No new session, first_fight_s, frame rate/frames, median/min FPS, RSS/VRAM, wait-site
ms/s or visual observations. No candidate retained/rejected and no speedup claimed.
Manual transient-artifact check remains missing; **30 FPS remains unmet**.

Blocker/next hypothesis: need blackout time into fighting, first versus later match,
mode/fighters and ideally a live-HUD black screenshot. Longer/repeated-match triggering
is unproven. Resume the prescribed bisect after positive reproduction, two clean runs
per clean verdict. Task B/C remain deferred; runner/supervisor left untouched.

### CURRENT: UFC Dev 094 — Task A still blocked; runner responsive (2026-10-06, 01:37)

Transfer paths, ufc-loop instructions and prescribed current/historical sections read.
No new blackout timing/scenario was supplied; the three-baseline stop condition below
still applies. Runner check `py -3.13 RunnerJob.py gitstatus`, job
`oc20261006-013729-gitstatus`, exited **0**. No UFC launcher/emulator or competing
build/test observed; caller KYTY_UFC_* overrides empty. No build, CTest or fight queued.
Installed **Dev 094**, branch dev, existing engine changes and all defaults preserved.
Files changed this invocation: HANDOFF.md and LOOP-LOG.md only. Focused/full tests not
rerun (latest supplied **53/53**); no new session/scenario/FPS/frames/median/min/RSS/VRAM,
wait-site or visual measurements. No candidate accepted/rejected or improvement claimed.
Manual transient-artifact check remains missing; **30 FPS remains unmet**.
Blocker: need actual blackout timing, first/later match, mode/fighters or a live-HUD black
screenshot before positive reproduction and the prescribed two-clean-run bisect. Longer
or repeated-match triggering remains unproven. Task B/C remain deferred; do not reset
the three-baseline count on the next transfer invocation.

### CURRENT: UFC Dev 094 — transfer re-entry; reproduction blocker unchanged (2026-10-06)

Read the NEXT-STEPS-DEV094 plan, local transfer settings, and current evidence. Its Task A
three-baseline stop condition has already been reached (details immediately below); the
new transfer request supplies no additional blackout timing/scenario. Do not repeat the
same baselines or infer a guilty feature from negative-only runs. Task B/C remain deferred.

Runner availability was verified, not inferred from process-name matching:
`py -3.13 RunnerJob.py gitstatus`, job `oc20261006-013556-gitstatus`, **exit 0**.
No UFC launcher/emulator or competing build/test was observed; caller KYTY_UFC_* overrides
were empty. No build, CTest or fight was queued. Installed **Dev 094**, existing uncommitted
engine work and all defaults remain unchanged. This invocation changed HANDOFF.md and
LOOP-LOG.md only. Focused/full tests: not rerun; latest supplied result **53/53**. No new
session, FPS/frame counts, RSS/VRAM, wait-site or visual measurements; reuse the three-run
evidence below without calling it a fix. Manual transient-artifact check remains missing.

Next actionable input: approximate time into fighting when the scene blacked out, first
versus later match in the same process, mode/fighters, and ideally a screenshot with the
live HUD. A longer/repeated-match trigger remains a hypothesis. Resume the prescribed
bisect only after positive reproduction; two clean runs per clean verdict. No candidate
accepted/rejected, no performance improvement claimed; **30 FPS remains unmet**.

### CURRENT: UFC Dev 094 — Task A reproduction limit reached; bisect blocked (2026-10-06)

Read `_Build/agent-reports/NEXT-STEPS-DEV094.md` first. This cycle completed the two remaining
`d094-all` baseline runs through the Claude runner, without changing engine code or defaults.
Together with `_Build/autolab/d094-all-20261006-011057`, **three 300 s all-on baselines have
not reproduced the black 3D scene**. This is a reproduction blocker, not a clean verdict for
any feature and not evidence that the user's fault is fixed. Do not restart the same three-run
baseline sequence next cycle or begin a negative-only bisect. Task B/C remain deferred.

Both jobs exited 0; 2560x1440/Mailbox, ShaderValidation/red-zone ON, Tracy OFF, a-p printed ON.
Ownership was clear immediately before each job; caller KYTY_UFC_* overrides were empty,
RunnerJob checked heartbeat, queue was empty, and no foreign processes were stopped.

| Session under `_Build/autolab/` | First fight / observed fighting (s) | Frames / whole-fight FPS | Opening first-45-s counter window | Fight median/min FPS | RSS / device VRAM (MiB) |
|---|---|---|---|---|---|
| `d094-all-20261006-012113` | 101.89 / 198.95 | 3741 / 18.93 | 2479->3168, 103.25->146.38 s: 689/43.13 = 15.97 FPS | 19/7 | 11111.8 / 13321 |
| `d094-all-20261006-012712` | 101.92 / 199.05 | 4095 / 20.73 | 2492->3150, 103.45->146.50 s: 658/43.05 = 15.28 FPS | 22/12 | 11017.7 / 12503 |

Saved darkness scanner: 0 black candidates in 37 post-detection HUD screenshots per run;
maximum dark fractions 0.548880 / 0.526875 (threshold >0.85, requires two consecutive shots).
Standing/ground samples visibly show Makhachev/Della Maddalena and T-Mobile arena; existing
skin/cage speckling remains. Ground transition occurs by 144.16 s in run 2 versus ~182.01 s
in run 3, so even opening windows are not identical workloads. No causal FPS/resource gain
is claimed. No device loss, driver events, matched FATAL/ERROR, or shader-validation failure;
triage's one SPIR-V hit per run is the known-valid-cache informational line.

Last consecutive 30 s wait reports (#8->#9), run 2 / run 3, ms/s:
gpu_untagged 18.35/3.06, cpu_write_flush 2.25/0.47, cpu_read_fault 0.10/0,
cp_read_fault 37.97/49.32, other_threads 54.14/61.35; srt_read_fault 0/0.
color_metadata, indirect_read_fault and args_copy have no counted waits in these logs.
Final gpu_indirect 16909/17237 proves the indirect path is exercised; these are mechanism
observations, not isolated performance acceptance. Sparse screenshots cannot establish
transient-artifact correctness; **manual transient/ground-and-pound check still missing**.

Retain/reject: installed Dev 094 and all pre-existing uncommitted work preserved; no new
candidate to accept/reject and no build performed. Focused/full CTest not rerun for unchanged
code; latest supplied Dev 094 result remains **53/53**, not a new test result. Only HANDOFF.md
and LOOP-LOG.md were edited this cycle. **30 FPS remains unmet.**

Next hypothesis/blocker: the user's blackout may need a longer match, repeated matches in
one emulator process, a different mode/fighters/camera, or different manual input. Need the
approximate time into fighting, whether it was the first or a later match in the same process,
and mode/fighters (ideally a black-scene screenshot to validate the detector). Once reproduced,
resume `d094-bis-abcdefgh`, two clean runs per clean verdict, then halve to one letter;
only then return that feature to opt-in and build/test/two-fight validate through the runner.

### CURRENT: UFC Dev 094 — runner-based black-scene investigation (2026-10-06)

The user supplied `../../UFC 5 Emulator Dev 094 Black Screen, Next Steps and Video Lag.md`
and `_Build/agent-reports/NEXT-STEPS-DEV094.md`; these supersede older build identities,
switch defaults and automation ownership rules. Dev 094 is installed. All a-p experimental
features default ON; preserve the retained ISA/BDA/viewport work. All builds, CTest and fights
must use `py -3.13 RunnerJob.py ...` through the running Claude worker. Never stop the worker
or run the build/test/game scripts directly. The user requested overnight continuation.

First local OpenCode reproduction, queued through the runner:
`_Build/autolab/d094-all-20261006-011057`, job `oc20261006-011034-fight`.
300.78 s, fight detected 102.00 s, 198.78 s observed fighting, **2601 frames / 13.20 FPS**,
opening counter window **606 frames / 43.22 s = 14.02 FPS**, sampled median/min 14/2,
peak RSS 15303.3 MiB and device VRAM 15062 MiB. Resolution 2560x1440, Mailbox,
ShaderValidation/red-zone ON, no Tracy; a-p printed ON. No device loss or driver events.

The guide's darkness heuristic found **0 black candidates in 36 post-detection HUD shots**
(maximum dark fraction 0.610052; threshold >0.85). Standing, low-FPS standing and ground
samples were visually inspected; the scene remained visible. This is **one screened-clean
run, not a cleared feature or proof against intermittent corruption**. Loading screens are
excluded. The detector has still not been validated on a real black-scene positive.
Reusable read-only scan: `_Build/agent-reports/dev094-black-scene-scan.py <session>`.

Next: repeat `RunnerJob.py fight --label d094-all --monitor-seconds 300` up to two more
times until a confirmed black run. Only then apply the feature-halving labels from the guide;
each clean bisect verdict needs two runs. If three baseline runs cannot reproduce it, record
the blocker and request the user's reproduction timing when they return; do not label a
feature clean or silently change defaults. Video/intro compute and readback bottlenecks remain
hypotheses requiring an intro-specific Tracy capture. No engine patch, C++ rebuild or new
performance improvement was claimed in this cycle; the supplied Dev 094 guide records 53/53.
Evidence: `_Build/agent-reports/dev094-overnight-start-20261006.md`.

### Earlier: UFC Dev 086 — Claude session 4 (2026-10-05, paused by user)

Installed Dev 086 (53/53). Defaults changed: **BDA keep ON** (GC never retires buffers over pages a BDA
shader faulted on; zero-read pages 11-25k -> ~400 per fight; `KYTY_UFC_BDA_KEEP=0` disables). Read-fault
attribution shows GPU-thread drains are mostly DrawIndirect argument reads; `KYTY_UFC_GPU_INDIRECT=1`
looked faster in its first run (14.15/17.73 vs 13.84/16.26). Explosions still occur intermittently
(bdakeep+dlon-b) and are not explained by BDA zero reads alone. FILL_CONVERT/GPU_DCC: no gain and a
boot device loss in combination: keep off. Full details: `LOOP-LOG.md` "Claude session 4".

### Earlier: UFC Dev 083 — Claude session 4 (2026-10-05)

Read `LOOP-LOG.md` "Claude session 4" first. Summary:
- **Dev 081 accepted**: cycle-28 dirty-log page-footprint fix (53/53; fight b 15.86 FPS / first 45 s
  12.75, same as Dev 080). Dirty log stays OFF by default.
- **Mesh explosions are a baseline, intermittent bug, not the dirty log.** Confirmed by screenshots on
  the Dev 044 source (`dev070-044src-b`, followed by device loss), Dev 078, Dev 081 (dirty log OFF) and
  Dev 082 (dirty log ON with every missed upload repaired). Earlier "Dev 044 clean / dirty log =
  explosions" conclusions came from too few runs. Explosions plausibly drive the GPU TDRs (giant
  triangles, GPU 100%, then device lost). Detector: `skinscan2.py` (copied to
  `_Build/agent-reports/skinscan2.py`): a frame scoring >= 0.3 is almost always an explosion;
  0.22-0.3 needs a look. The low-FPS+GPU-100% metric is not a reliable proxy.
- **Dev 082/083 diagnostics (all opt-in, defaults unchanged)**: `KYTY_UFC_BDA_DIRTY_VERIFY=1`
  (counts/repairs dirty-log misses; Dev 083 logs under the region lock so a miss is definitive),
  `KYTY_UFC_SRT_MEMO=shadow|1` (memo of resource materialization keyed on the user-data words
  actually read + replay of every guest read; Dev 082 shadow: 0 mismatches, 2% hits before the
  user-data mask), `KYTY_UFC_MPROTECT_INVALIDATE=1` (guest mprotect over GPU-mapped memory would
  drop tracker watches; Dev 083 always logs such calls). AutoFight label switches: `dlverify`, `dlon`,
  `srtshadow`, `srtmemo`, `gcoff`, `mpinv`. Manual dirty-log launcher: `_Build/Test-DirtyLog.bat`.
- Full CTest **53/53** on the Dev 083 source. Memory-tracker log entries now coalesce when contiguous
  (test updated to alternate non-adjacent pages for the overflow check).

### CURRENT: UFC Dev 080 — empty-EXEC readfirstlane ISA fix (2026-10-05)

Retained one bounded correction in `backend/spirv/spirvEmitterFlow.cpp`:
`v_readfirstlane_b32` reads guest lane 0 when EXEC is empty, instead of using an
undefined empty-ballot shuffle selector (observed lane 31 before the fix).
Nonempty least-active-lane selection, wave64 halves, Dev 079 DPP16, Dev 078
tail-branch decoder and every Dev 077 safe default remain intact. This is a
demonstrated ISA correctness fix, **not a proven FPS gain or UFC TDR cure**.

`tests/ShaderRecompilerComputeTests.cpp`: **8/8 new real Vulkan fixtures pass**
after a reproduced pre-fix failure (GPU-loaded distinct source values; empty,
low/high/both-half EXEC, wave32/wave64). Full `kyty_tests` build and CTest
**53/53, 54.70 s**; standard Build-Windows -SkipTests build/install succeeded.
Evidence summary: `_Build/agent-reports/dev080-readfirst-verification.md`;
detailed suite log `_Build/windows/Testing/Temporary/LastTest.log`.

Two serial 420 s, 2560x1440/Mailbox/ShaderValidation, no Tracy, default controls:
- `_Build/autolab/dev080-readfirst-a-20261005-110252`: HUD **128.50 s**,
  292.17 s fighting, **4157 frames / 14.31 FPS**, opening **517/42.57 = 12.14**,
  median/min 14/6, RSS **10700.2 MiB**, VRAM **12256 MiB**. Delayed-start/non-fast
  reference; new binaries incurred validation 1079 ms and pipelines 33707 ms.
- `_Build/autolab/dev080-readfirst-b-20261005-111014`: HUD **103.23 s**,
  317.52 s fighting, **4879 frames / 15.43 FPS**, opening **573/43.66 = 13.12**,
  median/min 14/8, RSS **10792.7 MiB**, VRAM **12295 MiB**. Fast mode, standing
  opening matches Dev 079 A; within Dev 079 12.90/13.17 opening range. No gain
  or resource reduction established. Repeat validation 65 ms, pipelines 742 ms.

No device loss, driver events, in-fight guest fault or shader-validation failure.
Both logs have the known timeout-teardown `SymInitialize FAILED 0xc0000004`.
Consecutive fully-fighting wait deltas A #5->#6 / B #4->#5, 30 s:
color_metadata **115.97/117.81**, gpu_read_fault **51.58/49.26**, other_threads
**353.79/358.80 ms/s** (site costs, different fight phases, not exclusive times).
Same Makhachev/Della Maddalena/T-Mobile octagon; standing/ground samples inspected,
old speckling/trails remain. **Manual transient-artifact/ground-and-pound check
still missing. 30 FPS remains unmet.** No opt-ins enabled, quality cuts, commits,
foreign process kills or supervisor changes; ownership/heartbeat rechecked and
CTest/gameplay serialized. Code/test files above, normal build counter and status
docs are this cycle's changes; pre-existing uncommitted work preserved.

Next: historical DrawIndex timeout suspects have **zero shader hashes**, only
guest PS/ES addresses with no matching hash entry/dump found. Resolve graphics
program/dump identity before choosing a hang-specific loop/EXEC fix; actual UFC
empty-EXEC readfirstlane coverage is unknown. P2 missed-upload fixture remains an
independent priority; dirty-log stays OFF until regression and manual visual proof.
Detailed evidence, limitations and pre-implementation milestone: LOOP-LOG Dev 080.

### Previous: UFC Dev 079 — DPP16 inactive-source ISA fix (2026-10-05)

Retained one fix in `backend/spirv/spirvEmitterFlow.cpp`: DPP16 with BC=0/FI=0
suppresses its destination write when the source EXEC bit is clear, instead of writing
a result computed with zero. Uses the existing wave32/wave64-half ballot implementation.
DPP8, FI/BC overrides, row/bank masks and all Dev 077 defaults / Dev 078 decoder work
are unchanged. This is a proven correctness fix, **not a proven TDR/FPS fix**.

`tests/ShaderRecompilerComputeTests.cpp`: pre-fix real Vulkan failure reproduced;
**40 new DPP16 + 6 existing DPP cases pass**, full CTest **53/53** in 49.83 s after
`cmake --build _Build/windows --target kyty_tests`. Standard build/install succeeded.
Evidence: `_Build/agent-reports/dev079-dpp-focused.log`, `dev079-ctest.log` (UTF-16);
readable detailed full-suite log `_Build/windows/Testing/Temporary/LastTest.log`.
Only these two code/test files, status docs and normal `build-iteration.txt` changed
this cycle; existing uncommitted work preserved, no commit/push or supervisor changes.

Two serial 420 s runs, 2560x1440/Mailbox/ShaderValidation, default MenuDrive, no Tracy:
- `_Build/autolab/dev079-dpp16-a-20261005-103011`: HUD **103.25 s**, 316.99 s fighting,
  **4727 frames / 14.97 FPS**, opening **564/43.72 = 12.90 FPS**, median/min 14/12,
  RSS **10827.3 MiB**, device VRAM **12229 MiB**.
- `_Build/autolab/dev079-dpp16-b-20261005-103735`: HUD **103.21 s**, 317.31 s fighting,
  **5762 frames / 18.23 FPS**, opening **575/43.66 = 13.17 FPS**, median/min 19/7,
  RSS **11038.7 MiB**, device VRAM **12365 MiB**.

No GPU loss, driver events, guest crash, shader-validation error or stderr in either
run. P-core pinning/RedZone confirmed, actual KYTY_UFC_* overrides empty. Rechecked
ownership/heartbeat before each build/test/launch, no foreign processes stopped and
no CTest/game overlap. Consecutive fight reports #4->#5 (30 s): color_metadata
**112.27/126.09**, gpu_read_fault **49.41/58.91**, other_threads **342.90/383.79 ms/s**;
all site costs, not exclusive frame times. No drain/performance reduction claimed.

Same fighters (Makhachev/Della Maddalena), arena and initial standing camera as Dev 078.
B is already on the ground by 140.28 s, A still standing at 140.45 s, so even opening
windows differ in workload; whole-fight rates cannot prove improvement. Dev 078 opening
12.15/12.48, Dev 077 13.01: current results are compatible with variance, not causal gain.
Standing/ground screenshots inspected: fighters/referee/cage/HUD render; speckling and
trails remain. **Manual transient-artifact/ground-and-pound check still missing.**
Current RSS/VRAM are slightly higher than Dev 078 samples with different fight coverage;
no resource-use gain claimed. **30 FPS remains unmet.**

Next: resolve historical timeout DrawIndex VS/PS hashes against saved shader dumps,
audit EXEC/lane-dependent loop exits. Exact UFC coverage of this DPP16 correction is
unknown; clean runs do not establish a TDR cure. If unrelated, advance a reproducible
dirty-log missed-upload fixture independently, leaving all risky opt-ins OFF.
Detailed milestone, test coverage limits and wait-site table: LOOP-LOG Dev 079.

### Previous: UFC Dev 078 — updated AnyPS5 decoder adaptation (2026-10-05)

Compared `C:/Users/cramo/Downloads/AnyPS5-main (1)/AnyPS5-main` with current Kyty and the
previous comparison report (the earlier source directory is gone). Detailed shader/library
findings: `_Build/agent-reports/anyps5-update-comparison-20261005.md`.

Retained one engine fix in `frontend/decode/ShaderDecoder.cpp`: do not truncate decoding at
an earlier `S_ENDPGM` while a known forward branch reaches later code. Uses a furthest-target
scalar instead of a code-sized bitmap, explicitly bounds branch targets, and preserves Kyty's
code-span exit and disabled debug-branch contracts. Pre-fix regression failed; host CFG and
**12 Vulkan execution cases** now pass (wave32/wave64, early returns, tail backedges, loops).
Build/install Dev 078 succeeded; full CTest **53/53**, log `_Build/agent-reports/dev078-ctest.log`.

Two serial 2560x1440/Mailbox/ShaderValidation runs (Tracy off):
- `_Build/autolab/dev078-branch-tail-a-20261005-095232`: 420.72 s, HUD 103.36 s,
  317.36 s fighting, 4427 frames / **14.02 FPS**, first-45-s sample window **12.15 FPS**,
  fight median/min 14/2, RSS 10801.8 MiB, device VRAM 12260 MiB, no device loss.
- `_Build/autolab/dev078-branch-tail-b-20261005-100648`: 421.12 s, HUD 103.25 s,
  317.87 s fighting, 4866 frames / **15.39 FPS**, opening **12.48 FPS**, median/min 13/11,
  RSS 10792.5 MiB, device VRAM 12212 MiB, no device loss.

Fighters/HUD render in inspected standing and ground screenshots; old speckling/afterglow
remains. Automated samples do not prove absence of momentary mesh corruption. Keep every
Dev 077 safe default (dirty log and other risky opt-ins OFF). This is a demonstrated decoder
correctness fix, **not an established FPS gain or GPU-TDR fix**. The old Dev 077 opening
reference is 13.01 FPS; current openings are slightly lower, without a paired control or
recorded foreground/core-clock state to establish causation. **30 FPS remains unmet.**

The loop prompt/agent/auto-loaded engine review now use the 30-FPS Dev 077+ mandate and the
new AnyPS5 references. Supervisor process/heartbeat pre-flight waits without consuming cycles;
build/install refuses active games and no longer force-kills them on lock retries. Automation
tests **17/17**, agent config and dry-run commands validated. Preserve existing uncommitted
work; no commits/pushes. Next: intermittent TDR shader/ISA investigation; the AnyPS5 DPP16
inactive-source write condition is also a concrete candidate for an isolated failing fixture.

### Baseline: UFC Dev 077 — Claude handoff (2026-10-05)

Read `_Build/agent-reports/claude-handoff-dev077.md` first; it supersedes the Dev 070 revert entry below.
Mesh explosions = BDA dirty-log fast path (now opt-in, KYTY_UFC_BDA_DIRTY_LOG=1). Guest crash pc 0x9034b8c02 =
red-zone corruption, fixed by red-zone protection (default on, KYTY_UFC_REDZONE=0 disables), not a bad PROFILE.
SPIR-V validation cache cuts repeat-launch validation 5.5 s -> ~30 ms. GPU + present threads pinned to P-cores.
Open: GPU TDR mid-fight (pre-existing), dirty-log missed upload, translated-shader disk cache. 53/53 tests.

### REVERTED to Dev 044 source — UFC Dev 070 (2026-10-05)

A manual bisect by the user found that Dev 045-062 (Claude sessions 1-3 plus loop cycles 22-24) cause
intermittent fighter-mesh explosions, a boot-time GPU device loss (WriteInvalid at page 0) and a guest
null-read crash (pc 0x9034b8c02). Dev 033 and Dev 044 are clean. None of Dev 045-061 measurably raised
FPS. The working tree is now the Dev 044 source; later work is preserved under _Build/claude-bisect/
and the two Claude patches. Do not re-apply it wholesale. See LOOP-LOG "BISECT RESULT AND REVERT".

### Claude session part 3 — GPU-first indirect args — UFC Dev 061 (final tree)

Indirect draws now use the GPU-sourced path even when a submitted host copy exists
(`KYTY_UFC_GPU_INDIRECT_FIRST=0` restores Dev 055 order); `args_copy` waits went from ~17-26 s
per run to 0, every indirect draw is GPU-served, and visuals are correct. A same-mode control
build (Dev 060) shows **no FPS change**: those waits are off the critical path. Also: indirect
SGPR-location counters (UFC: 100% `no_fetch`, nothing to fix), per-thread Windows
power-throttling opt-out (`KYTY_UFC_POWER_THROTTLING=1` disables it; effect unproven), and the
Cycle 24 `GpuIndirectMulti` test fixed (it never compiled) and moved into `CheckRasterization`.
Full CTest **53/53** on Dev 061.

**Benchmark warning:** fight runs are bimodal on this PC: "slow" (fight HUD at ~116.4 s,
~10 FPS opening) vs "fast" (~103.3 s, 15-16 FPS opening) with identical CPU-seconds/second.
This is most likely CPU core/clock placement while the window is in the background, not code.
Only compare runs with the same `first_fight_s` mode, using the first-45-s window. Details,
run table and next hypotheses: LOOP-LOG "Claude session (part 3)".

### Cycle 23 — GPU-sourced indexed indirect draws, unexercised — UFC Dev 055 (final tree = Dev 055)

Indexed indirect draws with GPU-owned, copy-less arguments are issued as
`vkCmdDrawIndexedIndirect` (barrier-ordered, no drain/submit) when provably safe: 16/32-bit
indices, `INDEX_BUFFER_SIZE == 0`, non-quad, non-mesh, restart without index scan; 8-bit,
clamped, mesh, quad and custom-restart fall back to the CPU path. Index buffer binds at the
guest base from the cached allocation (never created here); `m_num_instances` resolve is now
args-size-aware. `KYTY_UFC_GPU_INDIRECT=0` disables. All Dev 045–054 work intact (BDA dirty
log, coherent fills, GPU DCC, non-indexed GPU indirect + host copies, counters extended with
`gpu_indexed`/`idx_fallback`, viewport/scheduler/descriptors). Full CTest **53/53** before
and after gameplay, incl. new index-binding asserts. Qualifying run
`_Build/autolab/dev055-indexed-retry2-20261003-235012`: 421 s, fight 116.41 s (≈116 s
scenario), 304.6 s fighting, median/min **10/8**, title-counter **3356 frames / 11.07 fps**
(Dev 054: 3229 / 10.68 — parity); RSS 10669 MiB, VRAM 12479 MiB; no device loss/errors;
visuals correct. Counters: `gpu_indexed`=0, `idx_fallback`=0 all run — indexed args were
always host-copy-served in this workload, so no drain was removed; fight `gpu_read_fault`
~17 ms/s, color metadata 0. Provisionally retained (no test/visual/stability/FPS
regression; benefit unproven). Two non-qualifying attempts first (boot-splash stall exit 0;
then guest-address-space reservation — waited 4 min). Next: attribute remaining
`gpu_read_fault` (suspect `DrawIndirectMulti` direct reads), then multi-draw GPU-sourcing
or prefer-GPU-with-ordering-proof to kill ~2 ms `args_copy` waits.

### Cycle 22 — GPU-sourced non-indexed indirect draws — UFC Dev 054 (final tree = Dev 054)

Non-indexed indirect draws with GPU-owned arguments are issued as `vkCmdDrawIndirect`
from the buffer cache (barrier-ordered, no drain/submit); indexed/multi/quad/mesh fall
back to the CPU path and `m_num_instances` resolves lazily. `KYTY_UFC_GPU_INDIRECT=0`
disables. All Dev 045–053 retained work intact (BDA dirty log, coherent fills, GPU DCC,
host copies + counters with new `gpu_indirect` outcome, viewport/scheduler/descriptors).
Full CTest **53/53** (before and after gameplay) incl. new `TryObtainGpuIndirectArgs`
asserts; build warnings pre-existing only. Run
`_Build/autolab/dev054-gpuindirect-20261003-230715`: 420.37 s, fight 116.43 s (≈116 s
scenario), 303.9 s fighting, median/min **10/8**, title-counter **3229 frames / 10.68
fps** (45 s windows 9.6–12.2); RSS 10994 MiB, VRAM 12730 MiB; no device loss/errors;
visuals correct. Counters: `gpu_indirect`=6788 (≈ all `unsubmitted` reads GPU-served),
fight `gpu_read_fault` ~10–28 ms/s (was 30–60), color metadata 0. Provisionally
retained — mechanism proven but no FPS gain claimed (single-run variance; old runs lack
the frame column for direct comparison). Next: A/B control (`KYTY_UFC_GPU_INDIRECT=0`)
plus repeat, then indexed indirect.

### Claude session part 2 — UFC Dev 049–053 (final tree = Dev 053)

Added on top of Dev 045–048: selected-slice metadata ownership check; GPU-side DCC clear-code
evaluation (compute check + `VK_EXT_conditional_rendering` clears, 0xff expansion on GPU;
`KYTY_UFC_GPU_DCC=0` disables); indirect-draw argument host copies recorded after compute/DMA
producers and used only when submitted and not stale (writes/BDA epoch). Dev 052's extra submit
per copy was rejected and reverted. Full CTest **53/53** (Dev 053), GPU DCC path covered by the
full color-metadata suite (`ColorMetadataClearOnGpu`), new `BufferCacheIndirectArgsCopy` case.
Final run `_Build/autolab/claude-dev053-goodsave-20261003-220948`: 420.6 s, fight 116.4 s,
304 s fighting, 12/8 FPS, **2194 frames in shots 40→70 (13.3 fps) vs Dev 043 1478 (9.0 fps)**
same scenario; RSS 10.8 GB, VRAM 12.5 GB; no device loss; visuals unchanged. Fight color-metadata
waits are 0 (were 70–85 ms/s). If a 57 s guest fault repeats on every launch, restore the
PROFILE save (see LOOP-LOG part 2) before blaming code.
Handoff to OpenCode: `.opencode/agents/ufc-loop.md` and the supervisor PROMPT in
`Run-AutonomousLoop.py` were rewritten for the Dev 053 state (old copies in
`_Build/claude-runner/pre-handoff-backup/`). AutoFight now logs the window-title `frame`
counter and FightMetrics reports `fight_frames`/`fight_frame_rate` (automation tests 12/12).
Stop `_Build/claude-runner/Claude-JobRunner.ps1` before starting the loop.

### Claude session — UFC Dev 045–048 (BDA dirty log, drain diagnostics, metadata fills)

Interactive session with the loop paused; details and tables in LOOP-LOG "Claude session".
Retained (uncommitted, on `dev`): BDA CPU-dirty log (sync only logged/new ranges, walk as
fallback), `TileGetTextureSize` memo, Tracy stall zones + 30 s `GPU waits (...)` console
counters, uniform-fill compute over RT color metadata executed as `FillBufferCoherent`
(guest + GPU copies both updated, so RT discovery reads the clear code without a drain;
`KYTY_UFC_FILL_CONVERT=0` disables), buffer GC age measured in frames below the critical
threshold. Loop cycle 21's `descriptors.cpp` provenance hoist is still in the tree and built.
Wave32 scalar-branch fix, BDA batch walk (fallback), Dev 041 runtime viewport untouched.
Full CTest **53/53** (Dev 045/046/047/048) with three new cases (dirty log, memo, coherent fill).
Runs (420 s, same settings): Dev 047 fight 17/12 FPS, **2828 frames in shots 40→70** vs Dev 038
2649 / Dev 037 2137 (same ~104 s scenario); Dev 048 10/7 FPS, **2190 frames** vs Dev 043
1478 (same ~116–128 s scenario). RSS ~10.7 GB, VRAM ~12.5 GB unchanged; no device loss/errors;
visuals unchanged (speckling persists). Tracy: BDA sync 87 → 5.8 us/call, DrawIndex 38 → 27 us.
Remaining GPU-thread drains: color-metadata readbacks ~70–85 ms/s caused by shaders binding
36–92 MiB writable buffers spanning RT metadata; indirect-draw arg reads ~35–50 ms/s.
Next: GPU-side DCC clear-code evaluation; then GPU-sourced indirect draws.
Runner: `_Build/claude-runner/Claude-JobRunner.ps1` (stop it before resuming the loop).
`_Build/windows/CMakeCache.txt` got two warning-suppression entries (backup beside the runner).

### Cycle 20 — scheduler fast path measured and retained — UFC Dev 043

No new engine code this cycle; verified cycle 19's pending patch instead of
stacking another change. `commandScheduler.cpp` empty-retirement fast path
and Dev 041 viewport patch intact. Build/install -SkipTests passed (Dev 043),
focused scheduler/resource **3/3**, full CTest **53/53** (40.50 s,
`_Build/loop20-tests.log`), `git diff --check` clean.
First 420 s attempt hit the known intermittent guest null fault (Job4,
0xc0000005, PC 0x9034b8c02, runtimeLinker.cpp:719 — identical PC/file:line on
a third host patch/job thread, confirming variance, not causation). Same-build
retry `_Build/autolab/loop20-scheduler-retry-20261003-185908/`
(2560x1440/Mailbox, default MenuDrive, ShaderValidation, Tracy off) survived
420.72 s with no device loss/errors: fight detected 128.56 s, **292.16 s** of
fighting, **8/5** median/min FPS, RSS 10696 MiB, device VRAM 13033 MiB.
VS/PS/CS totals 282/246/268 (viewport fix holding). Fighters/HUD render;
speckling persists, no visual regression. 8 vs 7 FPS is run variance, not a
claimed gain; expected, since one skipped timeline query is small next to
~20 us per-draw preparation. Next: command-buffer-level host
binding/dynamic-state caching with proper invalidation (per-draw structural
work is the evidenced fight-FPS target). Stable 60-FPS objective unmet; no
goal-reached file. Details: LOOP-LOG cycle 20.

### Cycle 19 — scheduler empty-retirement fast path, provisional retain — UFC Dev 042

Implemented ENGINE-REVIEW priority 2 instead of another profiling-only cycle:
`CommandScheduler::PopPendingOperations` now returns early (under its mutex)
when the deferred retirement queue is empty, skipping the per-draw/per-dispatch
Vulkan timeline query. Non-empty path, all waits, priority ops and cleanup are
unchanged; monotonic `KnownGpuTick` means skipping `Refresh` can only defer,
never prematurely run, a retirement. New `SchedulerTimeline` cases cover the
empty-queue no-op and a later retirement. Full CTest **53/53 passed** twice
(`_Build/loop19-tests.log`), focused scheduler/resource **3/3**, `git
diff --check` clean. Retained Dev 041 viewport patch untouched; no guest,
resolution, validation or quality change.
Two gameplay attempts at 2560x1440/Mailbox, default MenuDrive,
ShaderValidation, Tracy off, both non-qualifying with no fight FPS: CLEAN
`_Build/autolab/loop19-scheduler-clean-20261003-184214/` stalled on the boot
splash and exited at 409 s with no fault/device loss; RETRY
`_Build/autolab/loop19-scheduler-retry-20261003-185044/` hit the known
intermittent guest null fault at 59 s (Job6, 0xc0000005, PC 0x9034b8c02,
runtimeLinker.cpp:719 — identical PC/file:line as cycle 17 on a different
patch). Neither establishes patch causation, so the patch is retained, but no
performance/resource/quality gain is claimed.
Next: one clean 420-second same-settings comparison of Dev 042 before any
further engine change; unchanged-code control only if the known fault recurs.
Stable 60-FPS objective unmet; no goal-reached file. Details: LOOP-LOG cycle 19.

### Runtime viewport engine upgrade — UFC Dev 041

Retained substantive shader/pipeline change: animated clip-space viewport scale/offset
is passed through four runtime push-data words instead of compiled into native shaders.
Header words are reserved ahead of stage resources (and after mesh draw data). Static
normalization extents remain specialized. Full CTest **53/53 passed**.
Actual run `_Build/autolab/engine-runtime-viewport-20261003-181440/` survived 300.47 s,
177.91 s of fighting, no device loss/matched errors. Fight FPS remains **7/5** median/min.
VS variants **1052 -> 286** versus the previous merged clean run; animated UI source
`a193f5b64015ea12` **194 -> 1**. Scene coverage differs, so don't claim a 73% FPS/loading
gain. Menus/fighters/HUD render; speckling persists. Code and subsystem review plus next
engine priorities: **ENGINE-REVIEW.md**. Preserve this verified change.
The CLI is now 1.18.34; a free Muse read-only heartbeat succeeded, fixing the old 426
upgrade-required fallback failure. Continue engine upgrades after this handoff.

### Cycle 17 — rejected literal SRT fast path, restored UFC Dev 038

Tested one C++ optimization: direct U32 literal evaluation in SrtWalker::Arg.
Rejected after separate profile attempt exited before fighting with a Job2 guest
null-access fault; causation unproved. Removed only this cycle's engine/test patch;
no semantic C++ diff remains. Wave32 scalar mask fix, BDA and integrated work preserved.
Changed content: this handoff, LOOP-LOG.md, generated counter (36 -> 38); engine/test
files may remain status-modified by line-ending normalization, but their git diff is
empty. No tooling/config/supervisor edits, commits or pushes. Details: LOOP-LOG cycle17.

Experiment **037** build/install passed, focused **3/3**, final full CTest **53/53**;
rollback **038** build/install and full CTest **53/53** passed. Initial test-shell
timeout left an orphan and contaminated a repeat with guest commit-reservation
failures; final suites ran after it exited, no accepted new test baseline.
Logs: `_Build/loop17-build.log`, `loop17-tests-final.log`, `loop17-rollback-build.log`,
`loop17-rollback-tests.log` (UTF-16). No regression remains; kernel_file_system passes.

Experiment CLEAN: `_Build/autolab/loop17-literal-clean-20261003-171858/`.
2560x1440/Mailbox, default short/long MenuDrive, ShaderValidation, TracySeconds 0,
crash diagnostics off/breadcrumbs on. Timeout **421.01 s**, fight detected **103.33 s**,
**317.68 s** fighting, **12/10 FPS** median/min, RSS **10852.4 MiB**, device-wide VRAM
**12811 MiB**, CPU **1.810 cores**, GPU **38%**. No device loss; shutdown SymInitialize
error in triage. PROFILE attempt `_Build/autolab/loop17-literal-profile-20261003-172621/`
exited **63.19 s**, no fighting/capture. No correction request produced; read console
and triage: Job2, 0xc0000005, PC **0x9034b8c02**, access=1/address=0,
runtimeLinker.cpp:719. No device loss; root cause unknown. No SRT timing comparison.

Latest restored CLEAN: `_Build/autolab/loop17-restored-clean-20261003-173058/`.
Same settings/schedule; timeout **420.37 s**, no matched errors/device loss. Detected
**103.35 s**, **317.02 s** actual fighting, **289 samples**, max gap **1.90 s**, no zeros.
Fight **16/9 FPS**, RSS **11076 MiB**, device VRAM **12888 MiB**, CPU **1.905 cores**,
GPU **43%**. Experiment standing vs restored grappling workload differs; neither higher
FPS than cycle16 nor lower patched FPS than rollback proves causation. No resource gain.
Reviewed shot_0051 in experiment/restored/cycle16: fighters/cage/crowd/HUD render;
speckled skin/canvas/shorts and suspect lighting persist; no matched quality gain.

Next: coarse **engine-only** allocation-origin/fallback-byte diagnostics on UploadCopies
staging Map failures, before changing staging strategy. Cycle16 BDA synchronization
3.492 s and Buffer constructors 1.381 s are evidenced but not caller-attributed.
Preserve all guest reads/checks; do not infer a shader-cache solution to steady fight FPS.
If Job2 fault repeats on restored code, investigate guest fault context separately.
Stable 60-FPS objective remains unmet; no goal-reached file. Cycle16 profile below is
still the valid merged hotspot capture; rejected experiment did not obtain a new one.

### Cycle 16 — merged-engine baseline, UFC Dev 036

First post-rebase clean gameplay baseline and separate fight-triggered Tracy capture
completed; **no engine/tooling/settings change** this cycle. Build/install -SkipTests
passed; focused resource/compute **3/3**, full CTest **53/53**, existing automation
**11/11**. Logs: `_Build/loop16-build.log`, `loop16-test-build.log`, `loop16-tests.log`
(UTF-16). Wave32/BDA and integrated work preserved. Changed only this handoff, loop
log and generated build counter (35 -> 36); unrelated externally appearing supervisor
file edits left untouched. Full commands/timings/next hypothesis: cycle 16 in LOOP-LOG.md.

CLEAN session: `_Build/autolab/loop16-merged-clean-20261003-165244/`.
Default short/long MenuDrive, ShaderValidation, **2560x1440/Mailbox**, TracySeconds 0,
crash diagnostics off, existing GPU breadcrumbs on. Timeout **300.37 s**, no matched
errors/device loss/driver events. Fight detected **129.70 s**, **170.67 s** observed,
156 samples, max gap **1.32 s**, no zero samples. Actual fight **7/5 FPS** median/min;
peak RSS **10674.6 MiB**, device-wide VRAM **12512 MiB**, GPU **35.5%**, CPU **1.810 cores**.

PROFILE session: `_Build/autolab/loop16-merged-profile-20261003-165752/`.
Same settings; timeout **300.89 s**, no matched errors/device loss. Late detection
**231.73 s**, Tracy started **232.87 s**, **30.16 s** / 27,018,158 zones captured.
Only **69.16 s** fight coverage, so NOT a qualifying 120-second benchmark; the clean
session above qualifies. Profiled fight **7/5 FPS**, RSS **10537.3 MiB**, device VRAM
**12531 MiB**. Use >=420 s for future profiles if delayed entry repeats.
Reviewed clean shot_0051/profile shot_0047: fighters/cage/crowd/HUD present; speckled
skin/canvas/shorts and suspect lighting persist. Different poses, no matched quality gain.

Merged inclusive hotspots: PrepareDrawRenderState **9.447 s / 19.714 us**,
ExecutePreparedDraw **9.486 s / 19.851 us**, MaterializeResources **5.730 s / 7.055 us**,
SRT refresh **4.254 s / 5.238 us**. Control-flow walk/evaluate **2.230 s** versus
setup **0.067 s**: vector-setup optimization is low priority. BDA synchronization
**3.492 s / 87.132 us**; Buffer construction **1.381 s / 14,839 calls**, caller not
attributed. Only seven translations (95 ms): shader compilation is not dominant.
Inclusive scopes overlap and do not prove exclusive host saturation or GPU waits.

Next: narrowly optimize redundant pure runtime-operand evaluation in SRT walk after
source inspection, retaining all guest reads/checks and per-generation memoization.
Alternative: diagnose BDA upload fallback allocation origins before any staging change.
No cross-draw cache without guest invalidation proof. Baseline evidence retained;
no performance/resource/quality improvement claimed. Stable 60-FPS objective remains
unmet; no completion file. Older integrated/no-benchmark text below is historical.

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
