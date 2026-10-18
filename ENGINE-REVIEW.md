# Emulator engine review and next upgrades

> **STALE MANDATE AND DEFAULTS. Current state: `HANDOFF.md` (Dev 124).**
> Written at the Dev 077/078 mandate; the tree is now **Dev 124** and the full suite is **56/56**,
> not 53/53. Two specific claims below are now false and have misled readers:
> - "Dirty-log sync caused mesh explosions and remains **OFF**" — the base BDA dirty log is now
>   **ON BY DEFAULT** (`bufferCache.cpp:53`, `UfcSwitch`; `=0` disables). The *generation* variant
>   (`KYTY_UFC_BDA_SYNC_GENERATION`) is the one that is genuinely opt-in/OFF.
> - "The Dev 077 baseline ... dirty-log, GPU indirect, args-copy, GPU DCC and fill-convert paths
>   are opt-in and OFF by default" — GPU indirect (`graphicsRun.cpp:869`), args-copy
>   (`bufferCache.cpp:61`) and table init (`bufferCache.cpp:126`) also default **ON** via
>   `UfcSwitch`. Only `BDA_SYNC_GENERATION`, `BDA_SYNC_MERGE`, `BDA_SYNC_VERIFY` and
>   `BDA_DIRTY_VERIFY` are `UfcOptIn` (default OFF).
> The "Subsystem review" table and "Continuous-loop priorities" below remain useful as review
> reasoning. Original text follows unchanged.

## Current mandate — Dev 077 baseline, Dev 078 decoder candidate, 30 FPS

Read the newest HANDOFF/LOOP-LOG entries and `_Build/agent-reports/claude-handoff-dev077.md`
before applying the historical measurements below. The objective is stable, correct **30 FPS**
actual fights at 2560x1440/Mailbox; it remains unmet. Dev 077 preserves red-zone protection,
known-valid SPIR-V caching and GPU/present-thread P-core pinning. Dirty-log, GPU indirect,
args-copy, GPU DCC and fill-convert paths are opt-in and OFF by default. Earlier mixed-mode
FPS comparisons and screenshot-only visual claims are superseded by the bisect findings.
The guest null-read at `0x9034b8c02` is red-zone corruption; do not restore PROFILE.

Updated AnyPS5 shader/library comparison and selected decoder adaptation:
`_Build/agent-reports/anyps5-update-comparison-20261005.md`. Its reference archive is
`C:/Users/cramo/Downloads/AnyPS5-main (1)/AnyPS5-main`. Preserve the validated tail-branch
decoder invariant and follow the latest acceptance status before stacking another change.
Check manual play and competing work before every build/test/install/launch; serialize CTest
and gameplay. New CLI cycles load these instructions fresh.

## Retained engine change: runtime viewport transforms (UFC Dev 041)

Animated screen-space viewport scale/offset no longer creates a new native vertex shader.
The renderer places four float words in the existing 128-byte push-constant bank; the
shader loads them at draw time. Host normalization extents, transform mode, wave size,
vertex formats and other semantic specialization remain in the key. Static transforms
remain available to the standalone compiler. Mesh draw words and resource data have
separate reserved ranges; oversized resource banks retain the existing storage fallback.

Files: shader input/static keys, shader compiler, SPIR-V module/position export, native
graphics program cursor, descriptor commit and draw submission. No guest work, resolution,
texture quality, validation or synchronization is removed.

Verification: full CTest **53/53 passing**; shader regressions verify valid SPIR-V,
identical keys and binaries across changing runtime viewports, different static extents,
and 128 simulated animated viewport positions. Actual 300-second UFC run:
`_Build/autolab/engine-runtime-viewport-20261003-181440/`, 2560x1440/Mailbox,
ShaderValidation, normal GPU breadcrumbs, no Tracy. No device loss or matched errors,
fight detected 122.56 s, 177.91 s of fighting, 7/5 median/min FPS.

Earlier merged clean run ended with **1052 VS variants / 1028 VS source entries**;
this run ended with **286 variants / 260 source entries**, each source entry having
a different guest hash. UI shader `a193f5b64015ea12`: **194 entries -> 1**. Observed
variant count is ~73% lower, but scene coverage/guest shader sets differ. The eliminated
viewport-driven source duplication and compiler invariance tests are the direct evidence.
Menus, fighters and HUD render in reviewed screenshots. Speckling remains. No causal
fight-FPS, RAM/VRAM, loading-time or image-quality improvement is claimed.

## Historical opt-in changes: GPU DCC clear evaluation, indirect-args host copies (UFC Dev 049–053)

- Render-target color metadata clear codes are evaluated on the GPU when the selected slices are
  GPU-owned (compute check + per-candidate clears under `VK_EXT_conditional_rendering`, same
  decision rules and 0xff expansion as the CPU path). Fight color-metadata drains: 70–85 ms/s -> 0.
- Ownership/readback covers only the selected metadata slices.
- GPU-written indirect draw arguments get a host copy after their compute/DMA producer; the copy is
  used only once submitted and when no later tracked write or BDA shader could have changed them.
  About a quarter of fight reads use it; the rest need GPU-sourced indirect draws.

Frame-counter comparison (fight starting ~116 s): Dev 053 2194 frames, Dev 051 2224, Dev 043 1478.

## Historical changes: BDA dirty log, metadata fills, drain diagnostics (UFC Dev 045–048)

Dirty-log sync caused mesh explosions and remains OFF; metadata fills are also OFF.
Frame-based GC, texture-size memoization and diagnostics remain enabled. Historical timings
below describe the opt-in experiments, not the effective Dev 077 default renderer.

- BDA synchronization walks only CPU-dirty ranges logged by the memory tracker plus newly
  registered buffers; the ordered batch walk remains the fallback (first call, log overflow,
  mapped-range change). Tracy: 87.1 -> 5.8 us per `SynchronizeBuffersInRanges` call.
- Uniform-fill compute dispatches over render-target color metadata run as
  `FillBufferCoherent`: clean ranges are filled in guest memory; GPU-owned whole pages are filled
  on the GPU and published to guest memory. Render-target discovery then reads the clear code
  without a full GPU drain. Proof obligations: `ResolveComputeBufferFill` (single store, uniform
  value, exact coverage), whole 4 KiB tracker pages, no submit (regression-tested).
- `TileGetTextureSize` per-thread memo; buffer GC age measured in frames below the critical
  threshold (no separate gain proven).
- Console `GPU waits (...)` every 30 s attributes blocking timeline waits by site.

Frame-counter comparison over the same screenshot window: Dev 047 2828 frames vs Dev 038 2649;
Dev 048 2190 vs Dev 043 1478 (see LOOP-LOG). Remaining fight drains: color metadata
~70-85 ms/s (imprecise GPU-modified marking from 36-92 MiB writable storage buffers spanning
RT metadata) and GPU-written indirect draw arguments ~35-50 ms/s.

## Subsystem review

This is a review of major paths and current evidence, not a claim that every line or game
has been audited. CPU scope timings are inclusive and cannot be summed as exclusive work.

| Area / source | Finding | Useful next upgrade |
|---|---|---|
| Shader cache: `shader.cpp`, `pipeline/pipelineCache.cpp` | Viewport-only variants caused avoidable shaders/pipelines; fixed above. Steady fighting has few compiles. Cache hits still materialize descriptors. | Preserve runtime viewport fix. Use persistent translated-code caching later for startup/stutter, not as the steady-FPS solution. |
| SRT/descriptor evaluation: `SrtWalker.cpp`, `ResourceMaterialization.cpp` | Per-draw host control-flow/operand evaluation is repeatedly performed. Existing memoization is per evaluation generation; setup is cheap relative to walk/evaluation. | Pre-resolve immutable dependencies or compile a compact host evaluation plan. Cross-draw reuse requires SGPR AND guest-memory write/invalidation proof; do not simply skip refresh. |
| Draw/descriptor preparation: `renderDraw.cpp`, `pipeline/descriptors.cpp` | Every draw re-resolves resources, prepares complete descriptor arrays, binds state and checks alias provenance. | Cache identical host bindings and dynamic state at the command-buffer level, with reset/layout/image-rebind invalidation. Retain alias checks and synchronization. Avoid blindly batching guest draws with changing resources. |
| Submission/retirement: `commandScheduler.cpp`, `masterSemaphore.cpp` | Empty-retirement fast path done (Dev 042). Real blocking waits are attributed by site. GPU-first indirect draws removed waits but produced no same-mode FPS gain and remain opt-in. | Diagnose the actual critical path; GPU-first indirect draws and unused occlusion queries are rejected UFC FPS levers. |
| BDA/stream uploads: `bufferCache.cpp`, `streamBuffer.cpp`, `renderContext.cpp` | BDA prepares all mapped cached ranges for shaders with arbitrary address reads. Upload-ring Map failures allocate temporary native buffers. Profiling shows constructor cost but insufficient caller attribution. | Reuse bounded fallback upload buffers with timeline-owned lifetimes, if allocation source is established. Do not grow staging unboundedly or remove dirty-memory checks. |
| Texture cache: `textureCache.cpp`, `image/imageView.cpp` | Texture/attachment acquisition refreshes content, tracks ownership, and resolves aliased images under a cache lock. Prior upstream changes address several alias/layout cases. | Reuse safe acquisition results within an operation/unchanged state and investigate ownership granularity. Changing cache identities or eviction without readback proof risks graphics corruption. |
| Native samplers: `samplerCache.cpp` | Complete descriptors are hashed under a mutex even on hits; descriptors can have force-point/native variants. | Per-command-thread last-used sampler cache keyed by full native descriptor and numeric border class, after confirming material cost. Smaller benefit than the top draw/SRT paths. |
| Guest GPU: `graphicsRun.cpp`, PM4 dispatch | Graphics/compute submissions and memory callbacks share an ordered GPU command lane. | Reduce parser/state-preparation overhead first. Parallel command recording requires immutable resource snapshots, queue dependencies and allocator isolation; more threads alone is not a safe upgrade. |
| Guest memory/kernel: `memoryAddressSpace.inc`, `pthread.cpp` | Windows reserves/commits 13.5 GiB guest backing, causing commit-pressure failures with concurrent tests/game runs. Guest CPU threads use host/native execution and guest ABI wrappers. | Longer-term: investigate demand commitment while preserving shared aliases and fault tracking. Diagnose recurring guest null PC separately; do not attribute it to every unrelated engine patch. |
| Audio: `libs/audio.cpp` | Upstream already carries fractional deadline/remainder handling and host streams. No current profile identifies it as the fight bottleneck. | Preserve timing correctness. Profile audio only if underruns/blocking guest threads are evidenced. |
| Rendering quality: shader image/flow emitters and fetch formats | Implicit derivatives under divergent control flow, wave64 graphics on a 32-lane host, temporary fetch formats and texture/RT history remain plausible speckle/lighting causes. | Target one identified shader/effect using captures and correct ISA semantics. Do not ship forced LOD0, disabled effects, or reduced quality as an optimization. |

## Continuous-loop priorities

1. Diagnose the intermittent GPU TDR using draw shader hashes, control runs and ISA-correct fixes.
2. Reproduce and fix dirty-log missed uploads; keep it OFF until regression/manual visual proof.
3. Versioned translated-shader disk cache for translation stalls, with complete state/metadata keys.
4. Shadow-verified per-draw SRT/resource reuse with explicit invalidation proof.
5. Bounded temporary allocation pools and identified shader/artifact fixes, separately measured.
The runtime viewport and scheduler fast paths are already implemented; preserve them.

No arbitrary speculative rewrite of the entire emulator. Each retained upgrade must change
engine behavior/work performed, have relevant regression evidence, and state the measured
benefit separately from hypotheses. 30-FPS correct gameplay remains the objective, not a
completion claim for this build. Keep captures/tests serial and compare like workloads.

## Loop readiness

The CLI was upgraded from 1.16.2 to **1.18.34** (desktop history/auth unaffected), clearing
the free-tier minimum-version blocker. A read-only free Muse request actually returned
READY after the upgrade. The existing supervisor uses Sol high first, paid Muse second,
free Muse third, with its existing cooldown policy and isolated database. No new loop
framework is required.
