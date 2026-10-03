# Upstream comparison and dev-branch integration

## Revisions

- Previous local base: `b3e419f` (`shader: implement 32-bit RDNA2 IMAGE_ATOMIC_CMPSWAP`).
- Latest upstream `KytyPS5/KytyPS5` main at comparison: `0e8ded3` (`window: ignore nonpositive resize dimensions`).
- `ChiefChriss/KytyPS5-UFC` main points to that same latest upstream commit. It had no fork-only commits or fork-only code improvements to merge.
- The user's local work was retained and integrated on `dev`, based on upstream `0e8ded3`.
- Upstream delta: **43 commits**, no fork-only commits.

## Upstream improvements included on `dev`

These are source changes in the upstream commit range, not claims that every feature is
verified with UFC 5:

### Performance and resource management

- Avoid synchronizing clean guest-memory ranges; query dirty bits without temporary snapshots.
- Fuse shader-resource specialization and buffer refresh work; collect shader-interface metadata
  in place and remove dead shader instruction cycles through linear liveness traversal.
- Pack runtime evaluation indices with IR opcodes and inline common IR operands/phi storage.
- Reuse sampler descriptor snapshots, SPIR-V arithmetic types, whole-wave ballot/first-lane
  calculations, graphics pipelines across inactive blend changes, and vertex shaders across
  equivalent layouts.
- Remove redundant per-dispatch checks/buffer resets and publish synchronous buffer downloads
  after ordered completion.

### Graphics correctness and compatibility

- RDNA2 image 64-bit atomic maximum and explicit gather-LOD selection.
- Preserve volume-texture slices, fix reused BC5 layout, clear single-mip targets when reused,
  preserve image contents across unrelated buffer aliases, and publish consumed color metadata
  without invalidating pooled images.
- Clamp compute LDS requests to device limits and fix Hades II startup/color clears.

### Platform, runtime, and input

- Windows CI, Linux CI, bundled dependency policy, AMD instruction fallbacks and guest red-zone
  liveness.
- Windows `PEEK`/`WAITALL` network receive behavior; this fixed the known
  `kernel_file_system` regression test on the integrated branch.
- Batched AIO completion, periodic kernel timers, POSIX thread stack attributes, shared sign-in
  dialogs, NetResolverAbort stub, and active NP Web API pool statistics.
- DualSense vibration/volume controls and smoother Bluetooth speaker/haptic handling.
- Audio accumulated timing-drift fix and nonpositive window resize handling.

## Local UFC work retained

- Wave32 native compute EXEC/VCC scalar-mask branches now make wave-wide decisions; regression
  case `Wave32ScalarMaskBranchesPreserveInactiveLanes` verifies masked lanes survive scalar flow.
  This is the fix that stopped UFC 5's original repeatable post-title GPU hang.
- GPU checkpoint/device-fault diagnostics and targeted suspect-buffer capture remain available.
- Image-view format-feature usage filtering and per-mip geometry changes are retained with the
  upstream additions.
- BDA synchronization uses one ordered cache-buffer walk over mapped ranges. The previous local
  measurement showed about 34% lower CPU cost per BDA prep call; upstream dirty-range skipping
  is now included too. Rebenchmark before attributing any combined performance change.
- UFC-specific active-controller selection, title/build labels, test fixture, logs, input
  automation, fight HUD/error watcher, Tracy reports, and serial OpenCode supervisor.

## Integration-only corrections

- Resolved five overlapping local/upstream edits by retaining both independent behaviors.
- Fixed the new upstream `Pm4NativeTargetGeometry` test's false assumption that SH register
  offsets `0x2`/`0x3` are removed legacy holes. They are active `SPI_SHADER_USER_DATA_ADDR_LO_PS`
  and `_HI_PS` registers. Kept the other legacy-hole and native PACE packet checks; added
  failure diagnostics that name any truly unexpected slot.
- `PrepareBindings` clears its per-call `buffer_sources` and `buffers` scratch vectors, as
  asserted by the upstream repeated-preparation test.
- Retained upstream's 64-bit atomic descriptor validation while allowing the local signed 32-bit
  atomic image case.

## Verification on integrated `dev`

- Windows Clang build and installation succeeded (`UFC Dev 035`).
- Full CTest: **53/53 passed** in 58.25 s. This includes upstream's `kernel_file_system`
  PEEK/WAITALL test; the local baseline had been 50/51.
- Automation harness: **11/11 passed**.
- During integration, an initial full CTest was run concurrently with a five-minute UFC process
  using ~11 GiB RAM; nine tests failed to reserve 13.8 GiB guest virtual memory. After that game
  process exited, the resource issue disappeared. Two actual code/test integration issues were
  corrected; the subsequent full run passed 53/53.
- Most recent completed UFC run predates the upstream rebase (snapshot 033): 300 s, fight reached,
  no device loss, 7 FPS fight median/min 5, RSS 10.6 GiB, VRAM 12.6 GiB. This is not a benchmark
  of the merged 035 tree. No FPS, memory, or rendering-quality gain is claimed for the merge.

## Branch disposition

The integrated work is on local branch `dev`, intended for
`ChiefChriss/KytyPS5-UFC:dev`. The fork's `main` branch is left unchanged.
