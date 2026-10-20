# UFC / upstream 099de8d integration

## Baselines and scope

- Integration directory: `E:\ps5 games\KytyPS5-UFC`, branch `dev`.
- Preserved fork: `943c545`, tag `dev124-goodfps-preserve`.
- Upstream target: `099de8d`; common ancestor: `0e8ded3`.
- Strategy approved by the user: upstream implementations for overlapping
  subsystems, including their headers, callers, and tests; selective fork ports.
- This document describes source integration, not proof of a GPU-hang fix or FPS gain.

## Upstream-owned dependency groups

| Group | Integration decision |
| --- | --- |
| Memory ownership, buffer readback, BDA synchronization | Use upstream `MemoryTracker`, `BufferCache`, `QueueBdaSync` / `TakeBdaSync` / `SynchronizeBda`. Remove the old dirty-log and async-readback interfaces from active code. |
| Texture metadata, image publication, compute fills | Use upstream texture-cache and render-executor implementations. Dev 124's image/backing publication defect remains a regression lead; replacing that path is not proof the defect is fixed upstream. |
| Indirect draws and EOP writes | Use upstream `IndirectDrawRegisters`, command-processor methods, and renderer callers. Do not reintroduce the old offset-location state or early-EOP experiment implicitly. |
| Descriptor binding and alpha export | Use upstream descriptor masks, binding layout, and `ShaderAlphaBlendSource` throughout. |
| Shader IR, resource materialization, SRT walker | Use upstream matching IR/value/pass implementations and tests. Upstream already has an Xbyak SRT walker/JIT. |
| Command recording and presentation | Use upstream scheduler/context/command-buffer interfaces and presentation locking. Retain the small empty-retirement-queue optimization separately. |
| Controller routing | Use upstream multi-player routing; the fork's old `m_connected_ids` / `CheckActive` implementation no longer matches it. Retain controller-name logging with the correct active-device ID. |
| Path-based truncation | Use upstream `KernelTruncate`; remove the duplicate fork definition/declaration left by auto-merge. Retain the fork's POSIX alias binding to that implementation. |

## Fork changes retained or re-ported

| Feature | Source integration / verification |
| --- | --- |
| Decode past early `S_ENDPGM` when a forward branch reaches later code | Retain the bounded furthest-target decoder fix. Restore its CFG regression on the upstream harness. |
| DPP16 inactive-source write suppression | Re-port using the upstream emitter's IR arguments; retain DPP8 zero-fetch behavior. |
| Empty-EXEC `v_readfirstlane_b32` | Re-port lane-zero selection using both ballot words, with upstream ballot/shuffle helpers. |
| Shader regression coverage | `ShaderUfcCorrectnessTests.inc`: 8 readfirstlane and 40 DPP16 cases; focused entry also runs 4 existing DPP8 negative controls. Cases are also included in the normal compute suite. |
| Fault-buffer initialization and overflow | Lazy zero-fill before first use; clamp CPU consumption to 1023 address slots; retain overflowed shader bits for another pass. Use upstream buffer registration, not the removed `HandleBdaFault`. |
| Partial GPU-mapped invalidation | Port matching `RenderContext` declaration and implementation; preserve the kernel mprotect caller. Snapshot intersections before callbacks to avoid holding the mapping lock during synchronization. |
| Image-copy geometry | Port bounded layer/depth counts and source/destination extent limits onto upstream Vulkan command handles. |
| Image-view usage filtering | Retain the compatible helper and caller. |
| Texture-size memoization | Retain the thread-local cache and its existing regression target. This does not enable mip trimming or reduce rendering quality. |
| Host scheduling | Retain GPU/present P-core helpers, guest-priority mapping, and timed-wait changes. Replace obsolete `KYTY_WIN_CS` guards with upstream platform guards. Historical default comments in Dev 124 were inconsistent; current source determines the settings. |
| Empty deferred-operation queue | Skip the timeline query when there is no ordinary retirement work; explicit waits and command-pool reuse retain upstream refresh behavior. |
| Native-image pool | Retain compatible allocator/header changes; remains opt-in (`KYTY_UFC_IMAGE_POOL=1`), pending integrated runtime verification. |
| Wait statistics | Retain self-contained counters; detach the old crash-reporter dependency. |
| Filesystem truncate support, build labels, background controller input, automation tools | Retain non-overlapping changes. Build guard recognizes upstream's `kyty_launcher` executable name. |
| Release IPO | Retain the opt-in CMake option used by `Build-Windows.ps1`. |

## Deferred ports: preserved, not silently enabled

The following Dev 124 source files remain available in the tree and in the
preservation tag, but are explicitly excluded from the emulator source glob:

- `renderer/cache/dccClearEvaluator.cpp`
- `renderer/gpuCrashDiagnostics.cpp`
- `renderer/pipeline/shaderDiskCache.cpp`
- `recompiler/NanTrace.cpp`
- `recompiler/ir/passes/SrtNative.cpp`
- `recompiler/ir/passes/SrtNativeMemory.cpp`

These depend on old recorder, resource-plan, shader-data, or metadata interfaces.
Merely compiling the preserved sources would reconnect incompatible designs.
The old recorder header and diagnostic support headers are likewise dormant.
The old frame dumper, dynamic viewport push-data scheme, SRT memoization, GPU
indirect/argument-copy experiments, and custom async readback/recording are not
active ports in this integration. Re-port each separately only with corresponding
tests and a measured or demonstrated benefit. CLI diagnostic flags inherited
from Dev 124 do not establish that the deferred diagnostic hooks are active.

The original failing image-publication reproducer is preserved in
`943c545:tests/ShaderRecompilerComputeTests.cpp` (`--image-backing-bda-only`).
It uses old buffer-cache interfaces and needs adaptation before it can establish
whether upstream has equivalent behavior. Do not label the publication issue fixed.

## Verification

Use a fresh build directory: the transferred `_Build/windows/CMakeCache.txt`
contains the other PC's source/compiler paths. Do not reuse it as build evidence.

Suggested dependency-group checks after configuring `_Build/upstream-099de8d`:

```powershell
cmake --build _Build/upstream-099de8d --target kyty_tests launcher --parallel 2
ctest --test-dir _Build/upstream-099de8d --output-on-failure -R "memory_tracker|page_manager|resource_materialization|resource_tracking|scalar_provenance|command_scheduler_timeline|buffer_cache_ranges|ufc_shader_correctness|texture_mip_trim"
ctest --test-dir _Build/upstream-099de8d --output-on-failure
```

Run gameplay only after the integrated build/tests, under a bounded manual or
explicitly requested session. Match fighters, arena, settings, and measurement
window; check geometry, frame pacing, GPU loss, and shutdown. Dev 124's recorded
17–20 FPS and unresolved visual failures are context, not proof of a causal gain
or a stable baseline.

Current verification results will be appended here as checks complete. No merge
commit or installation is implied by a conflict-free source tree.

### Build results

- Fresh CMake configuration succeeded with clang-cl 19.1.5 and Qt 6.10.3.
- The integrated `kyty_emulator.exe` compiled and linked successfully in
  `_Build/upstream-099de8d`.
- A subsequent launcher-only build succeeded, including Qt runtime deployment:
  `_Build/upstream-099de8d/launcher/kyty_launcher.exe`. The launcher can find the
  emulator in its parent directory; no manual executable copy is required.
- The build-guard Python regression passed (1 test).
- The full `launcher kyty_tests --parallel 2` build was not completed. It first
  exposed the duplicate `KernelTruncate`, then a Windows portability problem in
  upstream `VdecswTests.cpp` (`std::aligned_alloc`). The duplicate was removed,
  and the test now uses matching C++ aligned allocation/deallocation. A later
  build advanced through the compute-test source and timed out while compiling
  another full-emulator test target. This is not a CTest pass.
- After the timeout, the user asked about the delay and missing launcher. The
  launcher was completed separately; the large test build was not resumed.
- Full CTest, the adapted image-publication reproducer, and UFC gameplay/FPS
  verification remain outstanding. No gameplay loop was restarted.

### Completed build and packaging

- Completed `launcher kyty_tests --parallel 6` after the user's request to finish
  the build. All emulator, launcher, updater, and test executables linked.
- Full CTest initially exposed Windows error-code differences in path truncation
  and extended socket options, plus unquoted child-process argv[0] in a test whose
  build path contains spaces. Corrected these issues without dropping assertions;
  added more specific truncate diagnostics and nested-path coverage.
- Final full suite: **58/58 passed**, 45.16 seconds. Includes the 52-case focused
  UFC shader test and the normal compute suite. This supersedes the outstanding
  build/CTest entries above; gameplay and the adapted publication fixture remain
  unverified.
- Installed the complete runtime package to
  `E:\ps5 games\KytyPS5-UFC\_Build\UFC-Upstream-099de8d`.
  Both `launcher.exe` and `kyty_launcher.exe` are present beside the emulator,
  updater, Qt DLLs/plugins, assets, and winpthread runtime.
- Added `Launch-UFC-Upstream.cmd` at the project root, which starts the packaged
  launcher with the package as its working directory. The package-local `Kyty.ini`
  points directly at `E:/ps5 games/UFC5-extracted` and retains the current system
  configuration's global preferences. It does not change the system INI.
- Verified the packaged emulator's `--help` exits 0. Opened the packaged
  `launcher.exe`, observed a visible, input-ready main window, then requested a
  normal close; exit code was 0. No game was launched during this check.
- SHA256 comparison confirms the package contains the built emulator and
  launcher, not the older installed Dev 124 binaries. Recorded hashes and
  verification scope in the package's `BUILD-INFO.txt`.
- There are no remaining unmerged index entries or source conflict markers.
  Changes remain uncommitted. The existing iteration label is still Dev 124;
  that label alone does not identify the integrated binary.
