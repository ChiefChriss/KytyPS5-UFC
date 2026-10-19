# UFC on Senaxx: integration notes

This checkout is Senaxx/KytyPS5 `wolverine` at `4802886` (the build that reaches 20-33 FPS in
UFC 5), with selected upstream KytyPS5 commits and our UFC fixes on top. Nothing here is
committed; branch `ufc/senaxx-base-merge`. Senaxx's engine is kept as is: no upstream commit that
conflicts with it was taken.

## What is in this tree

| Change | Source | Why |
|---|---|---|
| 53 upstream KytyPS5 commits | upstream `main`, replayed oldest first | Only commits that apply cleanly and touch no file an earlier skipped commit changed (keeps every file's history consistent). Appendix A. |
| Wave32 scalar-mask branches decide once per wave | our Dev 124 (`spirvEmitterFlow.cpp`) | Senaxx made wave64 branches whole-wave but left wave32 compute branches per lane. That is what caused UFC 5's repeatable post-title GPU hang in our tree. Test `Wave32ScalarMaskBranchesPreserveInactiveLanes`. |
| f32 clamp modifier turns NaN into 0 | upstream `4f1f3b1bb3`, hand-ported | RDNA2 clamp is DX10 clamp mode; `GLSLstd450FClamp` leaves NaN undefined. A shader that relies on clamp to scrub NaN let it through, and a NaN pixel spreads through blur, bloom and temporal passes (the black spot that grows). Test `Vop3F32ClampFlushesNaN`. |
| Packed f16 min/max keep quiet NaNs out of the sNaN path | upstream `ba8b0c85b8` | A quiet NaN with payload bits was returned as if signaling, instead of the other operand. Tests in `PackedMinMaxF16NanAndSignedZeroEdges`. |
| 64-bit operands read float inline constants as f64 bits | upstream `b82512be06` | Integer B64 opcodes received `{f32 bits, 0}`. |
| 16-bit bit-typed reads of float inline constants use the half encoding | upstream `b6bd9ecae4`, hand-ported into Senaxx's `ReadU16LaneRaw` | Senaxx sliced the low 16 bits of the f32 pattern, so every inline float (0.5, 1.0, 2.0, 4.0, 1/(2*pi)) read as 0 there. |
| Pipeline prewarm | ours (`pipelinePrewarm.{h,cpp}`) | See below. |
| A button press makes a connected gamepad the active one | ours (`src/libs/controller.cpp`) | Senaxx only listened to the first connected pad, so a second pad or the automation virtual DS4 (MenuDrive) was ignored. |

## Pipeline prewarm

Every pipeline the game creates is appended to `_PipelineCache/<title>.prewarm` as it is created
(a crash or a killed run keeps it; the driver cache is only written on a clean exit). The next
launch rebuilds the list on background threads, and a draw or dispatch whose recipe matches
takes the prebuilt pipeline: it is neither compiled on the GPU thread nor skipped while Senaxx's
async compiler works on it. Fewer skipped draws also means fewer render targets left unwritten
for a frame.

Measured on our Dev 124 tree (same mechanism): cold run 756 pipelines in 85.5 s on the GPU
thread; next run 709 of 720 recipes taken, 3.1 s. Not yet measured on this tree.

- `KYTY_UFC_PIPELINE_PREWARM` unset or `1`: record and replay. `record`: record only. `0`: off.
- `KYTY_UFC_PIPELINE_PREWARM_THREADS`: builder threads (default a quarter of the logical
  processors, 1-4).
- The console prints `Pipeline prewarm: ... taken N waited N abandoned N misses N`.

## Automated comparison (2026-10-18, AutoFight-Senaxx.ps1, one run each)

| Run | Build | Fight median FPS | Fight average FPS | Min | Fighting | Device lost |
|---|---|---|---|---|---|---|
| 1 | this tree, cold (records prewarm) | 26.0 | 26.3 | 16 | 161 s | no |
| 2 | this tree, replay (702 pipelines prebuilt in 0.5 s) | 28.0 | 26.1 | 0 | 131 s, KO | yes, 270 s |
| 3 | original Senaxx 4802886, warm caches | 21.5 | 17.7 | 1 | 161 s | no |

Run 2's fight ended in a knockout; the device loss (NVIDIA fault, write to address 0 during a
draw) came on the post-fight stats screen, which runs 1 and 3 never reached. Not yet attributed;
repeat runs that reach the post-fight screen with and without `KYTY_UFC_PIPELINE_PREWARM=0`.

## Black spot (NaN) status

Not verified in game. The fixes above remove four concrete ways a shader here computes NaN or a
wrong constant where the PS5 does not. To check:

1. Run the same fight on this build and on the original Senaxx `4802886`; note whether and when
   the black spot appears.
2. If it still appears on this build, rerun with `KYTY_ASYNC_PIPELINES=0` and
   `KYTY_ASYNC_TRANSLATION=0`. If it disappears, a draw skipped while its shader or pipeline
   compiled left a target unwritten. That is Senaxx's async shortcut, not a math bug, and the
   prewarm and shader disk cache are the fix rather than turning async off.

## Not taken

- 300 upstream commits (Appendix B): they conflict with Senaxx's engine rewrites, or depend on
  one that does. `e6fce45d` (skip drains for CPU-only memory unmaps) merged textually but broke
  Senaxx's command scheduler, so it is excluded.
- Our wave-lowering interface: a structural refactor of our emitter with no speed or output
  change (verified byte-identical SPIR-V there). Senaxx's emitter is organised differently.
- One upstream test block, the R64 BC1 array footprint check from "gpu: admit block-compressed
  textures in PS5 R64 tiling": it needs `MakeTilingImage` from a skipped commit. The source change
  is in.
- Upstream `5b0ef077` (NGS2 sampler voices within max_waveform_blocks): its rework of
  `ngs2.cpp` needs the ATRAC9 decoder changes of a skipped commit and ended in a fatal
  "Not implemented" in the NGS2 tests. Senaxx's audio is kept.
- `TestFileDescriptorFlags` in `KernelFileSystemTests.cpp` is disabled: it expects the POSIX
  `fcntl` export from a skipped upstream commit.
- The rest of our Dev 124 engine work (SRT memo, BDA dirty log and so on): Senaxx has its own,
  faster versions (native SRT, tracker bitmaps).

## Appendix A: upstream commits applied (53)

Oldest first. "tests dropped" means the source change applied but its test edit conflicted.

- `1d41ddb378` shader: allow RDNA2 SDWA partial destinations for i24 multiply
- `77d7722173` shader: disable explicit MAD denormal checks for performance testing
- `dc3cd2d2c9` libNet: honour the option mask in sceHttpUriBuild (#1069)
- `57c97ebd86` Decode 16-bit color clears (TheCruZ/KytyPS5-GTA@b8dcd826)
- `afbb322b32` kernel: deliver persistent user events once per wait
- `7d2422abbe` add remote cheats launcher
- `fb7ff7ec03` Map UDS events to trophies from dumped metadata; trophy viewer/overview improvements (#1088)
- `4852721005` net: export connect, getsockopt, sendto, and recvfrom
- `35c6e8fff6` loader: enable the AMD CPU instruction patcher on macOS (#1014)
- `6ef406542c` tests: make kyty_tests build and run every test without LTO (#1110)
- `e679486ed3` net: translate native UDP socket options and microsecond timeouts
- `7415febd93` SDL title fix
- `e7cfe5a2f7` libkernel: implement sceKernelAioWaitRequests (#1109)
- `2a18e66a4c` gpu: validate depth copy state when drawing instead of decoding
- `d2413fc2eb` trophies: add viewer inspector and wrap toast titles
- `038d2257de` tests: enable image aliasing coverage on Linux
- `c30ac7c904` memory: avoid redundant Windows view protection
- `006875ed4f` graphics: pause memory-tracking spin loops
- `4d10ea17a0` graphics: streamline staging-buffer writes
- `c3a12397b0` tests: use function pointers in the concurrent backing read pairs (#1163) (tests dropped)
- `79a91ecfaf` graphics: cache the macOS thread id in TrackingSpinLock (#1169)
- `34f6fd1f5f` tests: cover the untested 16-bit integer ALU opcodes (#1151)
- `de62664908` kernel: report /dev/random and /dev/urandom as character devices (#1140)
- `861dd76d7f` net: accept sufficient Ethernet address output buffers
- `210d4dcd99` sysmodule: track shared module load references and unloaded status
- `3c62359e05` Fix/trigger effect state (#1132)
- `4865dd92ca` libNet: answer sceNpGetAccountLanguage2 as a signed-out user (#1186)
- `4e14eed770` kernel: start the semaphore wait timer after the early returns (#1173)
- `107d5f750a` libNet: finish NpCommerce dialogs at open (#1166)
- `1f3adf5109` cmake: accept glslang's current tool name and document Python 3 (#1161)
- `58978d154a` font: rasterize glyphs larger than 128 pixels (#1214)
- `b65dc12208` Track pad port ownership and reject duplicate opens
- `b35cdc39b7` shader: append deferred Phi operands directly to SPIR-V storage
- `7c6121cba5` shader: build CFG labels and targets from ordered storage
- `c1926f915d` update screenshot
- `f3836bb831` tests: fix the Linux build of ResourceMaterializationTests (#1333) (tests dropped)
- `053f5fd3cc` shader: emit DPP row_share instead of an identity move
- `1cfb9522a3` controller: use Back as the touchpad click on pads without a touchpad (#1355)
- `b7b335ac63` gpu: admit block-compressed textures in PS5 R64 tiling
- `7e4e142aae` gpu: enable native R8 SNORM texture sampling
- `ece4e28740` gpu: store private format byte sizes in uint8_t
- `a6f4e8e7f4` AvPlayer: recycle output buffers so the decoder can run ahead (tests dropped)
- `6f3bebcb50` playgo: read plgx chunks
- `c0ebf22476` tests: rename a local that collides with the Windows small macro (#1400)
- `3197ef093c` test: align scalar image tables and buffer reads with PS5 semantics (tests dropped)
- `aeecc9343a` test: give native binding fixture valid nonempty buffer descriptors
- `166b9a5eea` test: dispatch null texture coverage with the intended wave size (tests dropped)
- `6b1c4a7a71` test: model bounded scalar selector batches in native CFG coverage (tests dropped)
- `cbe5b62947` file: keep independent Linux file handles coherent
- `c6b8210d7c` test: fix the Linux and Windows CI test builds (#1504) (tests dropped)
- `5dec523143` Stub RazorCpu profiling markers in a dedicated library
- `42bb0da520` loader: keep the caller's flags in the TLS stubs (#1571)
- `32587337b5` renderdoc: advance capture flips independently of logging

## Appendix B: upstream commits skipped (300)

Reason is the first conflicting path, or the skipped commit it depends on.

- `5c83060481` shader: resolve masked image keys in strided descriptor records (src/graphics/shader/recompiler/ir/ShaderIR.h)
- `7a0e1dafd7` shader: classify native half values with shared operand decoding (src/graphics/shader/recompiler/backend/spirv/spirvEmitterAluHelpers.cpp)
- `266c12cec6` Revert "shader: keep scalar data native and preserve raw addresses" (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `5b826c6e4a` shader: emit native 64-bit operations where compatible (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h, src/graphics/shade)
- `e6fce45d3a` gpu: skip drains for CPU-only memory unmaps (excluded)
- `984010227f` graphics: make depth bounds dynamic to reuse pipelines (src/graphics/host_gpu/renderer/pipeline/shaders.cpp)
- `e5dcfdb87b` graphics: bound unbounded buffer reads and writes (#1066) (src/graphics/host_gpu/renderer/pipeline/descriptors.cpp)
- `cdb64bfd4f` graphics: share indirect draw handling and honor indexed sources (src/graphics/guest_gpu/graphicsRun.cpp)
- `9caec9f2c8` shader: preserve RDNA2 DLC coherence for flat address loads (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp)
- `c23de62184` shader: resolve nested descriptor selector tables with exact scalar offsets (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `cd06601022` shader: replace CPU selector chains with direct image-table lookup (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `5601861d67` shader: implement RDNA2 scalar 16-bit sign extension (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `5a491abbda` shader: remove redundant descriptor sorting and stale null check (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `ae1e92621d` shader: sample mixed bindless images through contiguous native arrays (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp)
- `b929bbe3e0` shader: limit broad image tables to compatible views (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `199d0a9d92` kernel: name guest threads on the host (#1106) (src/kernel/pthread.cpp)
- `9a5658c038` launcher: add per-game config import and export (src/launcher/src/configuration.cpp)
- `1195af2510` decoder: add V_CMPX_U_F32 (#1120) (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `1b21161aac` kernel: fix the macOS build of host thread naming (#1122) (depends on skipped src/kernel/pthread.cpp)
- `60e53a39cc` decoder: accept clamp on V_MIN3/MAX3/MED3_F16 (#1116) (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `7492cb9577` shader: emulate DS_ORDERED_COUNT as a GDS atomic (#1117) (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `cc44a43ebc` systemservice: add an option to report the notice screen as skipped (#1127) (depends on skipped src/launcher/forms/configuration_edit_dialog.ui, src/launcher/include/configuration.h, src/)
- `bb98f26433` shader: add missing I64/U64 compare variants (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `67d7db685a` agc: new abi (depends on skipped src/libs/agc.cpp)
- `5af370c257` shader: add BUFFER_STORE_FORMAT_D16_X opcode (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `26cce9909a` shader: add BUFFER_LOAD_FORMAT_D16_X opcode (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `96eb681b90` shader: add V_MIN3_U16 opcode (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `fbf6ae400c` graphics: preserve depth array layout during storage reinterpretation (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp)
- `3ba6ebae81` shader: initialize perspective sample barycentrics at SampleId (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp, src/graphics/shader/reco)
- `21a1346e27` graphics: route compact color exports through CB_SHADER_MASK (depends on skipped src/graphics/host_gpu/renderer/renderDraw.cpp, src/graphics/shader/recompiler/ir/passes/Sha)
- `b96871e2bf` shader: unpack compressed SNORM16 color exports as normalized signed values (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp)
- `a8beac190f` shader: evaluate scalar branch decisions across the complete guest wave (depends on skipped src/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp, src/graphics/shader/recompil)
- `5297abb7ac` shader: run non-amplifying NGG programs in mesh workgroups (depends on skipped src/graphics/shader/recompiler/frontend/translate/Translate.cpp, src/graphics/shader/shader)
- `9f83fd5f0c` graphics: apply indirect draw offsets to guest user SGPRs (depends on skipped src/graphics/guest_gpu/graphicsRun.cpp, src/graphics/host_gpu/renderer/renderDraw.cpp)
- `743fdc6990` graphics: synchronize raw storage reads with GPU image aliases (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp)
- `dd754065d8` graphics: preserve full tiled array backing in image-to-buffer copies (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp)
- `470669c677` graphics: make raw image synchronization an opt-in setting (depends on skipped src/common/emulatorConfig.cpp, src/common/emulatorConfig.h, src/graphics/host_gpu/renderer/)
- `190f838ea4` shader: preserve workgroup bounds for scalar image descriptor tables (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `9c79d9d298` memory: flatten direct and batch mapping paths (src/kernel/memory.cpp)
- `a87889f032` graphics: process upload regions in one traversal (src/graphics/host_gpu/memoryTracker.h)
- `7e011fa565` memory: allow concurrent backing reads (depends on skipped src/kernel/memory.cpp)
- `45b6d2c597` memory: copy sparse backing in one traversal (depends on skipped src/kernel/memoryAddressSpace.inc)
- `9e666e4dd2` shader: support scalar byte sign extension (depends on skipped src/graphics/shader/recompiler/frontend/decode/ScalarAluOps.cpp, src/graphics/shader/recomp)
- `7881c69a54` shader: decode v_cmp_class_f16 (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `9f25e3c23e` shader: support partial SDWA multiply and byte-source moves (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `e43aadf75f` ampr: assign unique IDs to resolved file paths (src/libs/libAmpr.cpp)
- `c86c8b4d93` audio: mix AudioOut2 objects and apply port gains (src/libs/libAudio2.cpp)
- `420bb51d6c` fix regression 9f83fd5f0ca23d1e968340f4f67b3113040158b9 (depends on skipped src/graphics/host_gpu/renderer/renderDraw.cpp)
- `db41d0482f` kernel: retain POSIX file descriptor close-on-exec flags (src/kernel/fileSystem.cpp)
- `5a8808086a` ampr: honor shared counter waits and completion writes (depends on skipped src/libs/libAmpr.cpp)
- `62643c3424` system service: deliver initial AppContent entitlement updates through shared queue (depends on skipped src/libs/libSystemService.cpp)
- `aa3b2cd57f` Implement AGC async rewind imports (depends on skipped src/libs/agc.cpp, src/libs/agc.h, src/libs/libAgcDriver.cpp)
- `f40755173f` ampr: keep resolved host paths in native form (#1175) (depends on skipped src/libs/libAmpr.cpp)
- `b38b7454a6` Preserve vertex export locations for mixed pixel interpolation (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/shader/recompiler/b)
- `2f4b8a335d` shader: allow SDWA byte destinations for bitwise VOP2 ops (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `52dbc724f8` shader: store SPIR-V definitions directly in IR nodes (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h)
- `e1ac893a0c` shader: collect usage once and emit descriptor slots directly (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp, src/graphics/host_gpu/renderer/pip)
- `dbe5353f3a` shader: eliminate retained SRT lookup aliases (depends on skipped CMakeLists.txt, src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompile)
- `97fda7b10f` renderer: bind prepared buffer descriptors without copying (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp, src/graphics/host_gpu/renderer/ren)
- `46997dfc7a` shader: retain resource type IDs in SPIR-V emission state (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAnalysis.cpp, src/graphics/shader/)
- `8abf99c56e` shader: emit bounded SPIR-V operands without temporary vectors (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp, src/graphics/shader/recom)
- `3df456675d` shader: own control flow on IR blocks with direct targets (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterProgram.cpp, src/graphics/shader/r)
- `ee4dd90649` shader: emit native integer operations and remove signedness casts (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp, src/graphics/shader/recom)
- `b4853a0e08` shader: preserve export vectors instead of rebuilding scalar components (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp)
- `13e3ca314c` shader: unpack buffer offsets from one load per packed word (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemoryHelpers.cpp)
- `393257eaae` shader: classify float NaNs with one magnitude comparison (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAluHelpers.cpp)
- `7b9997baea` shader: fold exact float class masks into native NaN predicates (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp, src/graphics/shader/recom)
- `1fc520cd62` shader: FLAT_STORE_DWORD (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp)
- `1bd282ec0a` shader: BUFFER_LOAD_SHORT_D16 (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `a31e2cd361` shader: BUFFER_LOAD_SHORT_D16_HI (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `3ae5a9b70a` agc: implement branch target patching and packet size queries (depends on skipped src/libs/agc.cpp, src/libs/agc.h, src/libs/libAgcDriver.cpp)
- `4c8fc53a85` agc: support one-word NOP reservations and share packet sizing (depends on skipped src/graphics/guest_gpu/graphicsRun.cpp, src/libs/agc.cpp)
- `488c0acae9` shader: resolve writable descriptors selected by immutable branches (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp, src/graphics/shader/recompil)
- `3b1a94707a` shader: capture only memory-dependent resource predicates (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `5b68a26cce` shader: keep scalar payload and terminal conditions on the GPU (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `71c7261740` shader: share indirect texture dispatch with image loads (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAnalysis.cpp, src/graphics/shader/)
- `973809cde0` shader: retain bounded scalar material image selectors (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `165cd884ca` agc: initialize function table in tracked driver memory (depends on skipped src/libs/agc.cpp)
- `1818aae562` shader: link scalar leaf calls only for SWAPPC programs (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/shader/recompiler/S)
- `bb5edbd490` shader: expose graphics compute queue fields to GETREG (depends on skipped src/graphics/guest_gpu/graphicsRun.cpp, src/graphics/host_gpu/renderer/render.h, src/graphi)
- `8c81386568` shader: share finite descriptor planning with GPU-selected inline samplers (depends on skipped src/graphics/shader/recompiler/ShaderRecompiler.cpp, src/graphics/shader/recompiler/backend)
- `3e9f88bf79` shader: isolate SWAPPC work from ordinary shader paths (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/shader/recompiler/S)
- `7da3c4ed21` launcher: add resizable game grid/improve controller setup (depends on skipped src/launcher/forms/configuration_list_widget.ui, src/launcher/include/configurationListWidg)
- `59183dbfa8` shader: add missing F64 VOPC compares (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h, src/graphics/shade)
- `1c301c9aab` Revert "shader: keep scalar payload and terminal conditions on the GPU" (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `5559cfcbb2` renderer: optimize tiling and reuse centralized image layouts (depends on skipped CMakeLists.txt, src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gp)
- `93452b24e3` renderer: cache dynamic graphics state (depends on skipped src/graphics/host_gpu/renderer/commandScheduler.cpp, src/graphics/host_gpu/renderer/command)
- `d247d3d60c` shader: complete signed 64-bit integer comparisons (depends on skipped src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h, src/graphics/shader/recompi)
- `2f4d0f383e` shader: add SDWA support for V_MIN_I32 (#1342) (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `3b9e176b70` shader: support VOP1/VOP2 DPP8 fetch-inactive (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `fa56c5eae9` shader: add scalar zero-bit count and search operations (depends on skipped src/graphics/shader/recompiler/frontend/decode/ScalarAluOps.cpp, src/graphics/shader/recomp)
- `679be25c37` shader: recognize remaining disabled debug branches (depends on skipped src/graphics/shader/recompiler/frontend/decode/ScalarAluOps.cpp, src/graphics/shader/recomp)
- `307fdcf7ad` add self updater (#1352) (depends on skipped src/launcher/src/mainDialog.cpp)
- `f63d3befdc` Add startup splash screen (depends on skipped src/common/emulatorConfig.h, src/libs/libSystemService.cpp, src/main.cpp)
- `947627d10d` docs: clarify pull request scope (depends on skipped README.md)
- `9a5dd9faff` agc: reserve driver backing before guest direct-memory allocation (depends on skipped src/libs/agc.cpp, src/libs/agc.h)
- `cf77fb4353` shader: initialize NGG vertex count from active native invocations (depends on skipped src/graphics/shader/recompiler/frontend/translate/Translate.cpp)
- `58ba1e4cb4` docs: update README wording and grammar (depends on skipped README.md)
- `448a4c8a6a` launcher: reorganize panels (depends on skipped src/launcher/forms/configuration_edit_dialog.ui, src/launcher/src/configurationEditDialog.c)
- `fbea51c684` agc: implement sceAgcSetNop (#1343) (depends on skipped src/libs/agc.cpp, src/libs/agc.h, src/libs/libAgcDriver.cpp)
- `d4bb7fd786` agc: remove command buffer debug dump (depends on skipped src/libs/agc.cpp)
- `730e1a3cf3` docs: add Fedora and Fedora Atomic (Bazzite) build instructions (#1372) (depends on skipped README.md)
- `f848d2761b` add srt walker (depends on skipped CMakeLists.txt, src/graphics/shader/recompiler/ir/Program.cpp, src/graphics/shader/recompil)
- `65144ce4ac` gpu: centralize depth copy state validation in target resolution (depends on skipped src/graphics/host_gpu/renderer/depthRenderTarget.cpp)
- `1029511bbc` add depth to color copy (depends on skipped src/graphics/host_gpu/renderer/depthRenderTarget.cpp, src/graphics/host_gpu/renderer/render)
- `cb375ec35a` add load exec (depends on skipped src/common/emulatorConfig.cpp, src/common/emulatorConfig.h, src/libs/libSystemService.cpp)
- `4084a008ea` fix windows compilation (depends on skipped src/graphics/shader/recompiler/ir/passes/SrtWalker.cpp)
- `6f0cddd440` implement LoadExec (windows) (depends on skipped src/common/emulatorConfig.h, src/loader/loadExec.cpp)
- `16a7fe689a` fix windows compilation (2) (#1411) (depends on skipped CMakeLists.txt, src/graphics/shader/recompiler/ir/passes/SrtWalker.cpp, src/loader/loadExec)
- `4c87dbebb2` shader: select indirect texture descriptors for image gathers (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `f28c0fccb2` texture: preserve guest DCC metadata for comparison depth aliases (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gpu/renderer/pipel)
- `df0412afb5` shader: preserve shared predicates during resource plan extraction (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `984cfcebd1` shader: emit native texel offsets using sample execution context (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `e51de54ee4` add hdr display support (depends on skipped CMakeLists.txt, src/common/emulatorConfig.cpp, src/common/emulatorConfig.h)
- `048fe9f413` kernel: export sceKernelFtruncate through existing native handler (depends on skipped src/libs/libKernel.cpp)
- `41beda4367` ampr: fix APR completion loss in 768-byte read batches (depends on skipped src/libs/libAmpr.cpp)
- `e699a35f20` netctl: add opt-in host IPv4 connectivity and launcher toggle (depends on skipped src/common/emulatorConfig.cpp, src/common/emulatorConfig.h, src/launcher/forms/configuratio)
- `5b730d4df0` simplify guest gpu (depends on skipped src/graphics/guest_gpu/command_processor/pm4Handlers.cpp, src/graphics/guest_gpu/graphicsRu)
- `dad900c61d` Fix BC alias writes and GC (depends on skipped src/graphics/guest_gpu/graphicsRun.cpp, src/graphics/host_gpu/renderer/cache/textureCache.c)
- `5ce9a52936` Fix the splash screen (#1378) (depends on skipped src/graphics/presentation/window/swapchain.cpp)
- `5b0ef07742` audio: keep NGS2 sampler voices within max_waveform_blocks (#1450) (excluded)
- `df5b9b92be` minor cleanup (src/common/threads.cpp)
- `f4692a450e` graphics: avoid full-cache scans when deleting stencil associations (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp)
- `8f7d745383` graphics: simplify image usage and dirty state (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gpu/renderer/image)
- `fd66f580b4` shader: resolve packed buffer selectors with shared indexed stores (depends on skipped src/graphics/presentation/window/vulkanWindow.cpp, src/graphics/shader/recompiler/backend/s)
- `fa68e104ea` shader: fold full wave64 masks before register predication (depends on skipped src/graphics/shader/recompiler/ir/passes/ConstantPropagation.cpp)
- `df08b83287` shader: propagate late constants through affected IR users (depends on skipped src/graphics/shader/recompiler/ir/passes/ConstantPropagation.cpp)
- `7688093ca3` shader: resolve GPU scalar image selectors within buffer bounds (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `49ebbfd627` shader: prove descending image ranges and prune impossible register selectors (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `652154aead` shader: merge typed image candidates as raw vectors with shared sampler variants (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `460a196fca` shader: emit dynamic sample offsets with the enabled maintenance8 feature (depends on skipped src/graphics/host_gpu/graphicContext.h, src/graphics/host_gpu/renderer/pipeline/pipelineCac)
- `d692d8ee08` shader: preserve full region addresses for LDS and GDS reads and writes (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp)
- `0e9c77b0a2` shader: bound material selectors by proven one-wave LDS append lists (depends on skipped src/graphics/shader/recompiler/ShaderRecompiler.cpp, src/graphics/shader/recompiler/ir/Shad)
- `f01a3750d4` shader: validate indirect image operands at each typed access (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `145578ac03` shader: materialize image keys from proven GPU bitmap subsets (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `e2c8b61de6` shader: remove unreachable mixed image specialization paths (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `a7a734b5d3` video: implement PS5 Vdecsw using a shared queued AVC decoder (depends on skipped CMakeLists.txt)
- `5dc6914688` shader: lower bounded UInt formatted buffer copies to shared stores (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `9a7c54da58` shader: share formatted buffer payload reads across format conversions (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp)
- `8ee31abd9d` shader: bound indirect image selectors by counted scalar rows (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `68d7bc94e2` shader: correlate masked first-lane material rows (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp, src/graphics/shader/recompil)
- `aec8c88755` graphics: support signed 32-bit image atomic descriptors (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp, src/graphics/shader/recompiler/ir/)
- `c11254a996` shader: share format conversion for packed normalized image stores (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp, src/graphics/shader/recompiler/bac)
- `56738aa4e1` shader: sample packed normalized images with one bilinear gather (depends on skipped src/graphics/guest_gpu/gpu_format.cpp, src/graphics/host_gpu/renderer/pipeline/descriptors.)
- `a287a54f26` shader: prove guarded descriptor counts for interleaved image records (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `25e1cc7ec9` shader: preserve descriptor bounds through guarded branch joins (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `ed1195d1e7` shader: prove descriptor bounds from simplified image EXEC predicates (depends on skipped src/graphics/shader/recompiler/ir/passes/ConstantPropagation.cpp, src/graphics/shader/recom)
- `9bcf80bf67` graphics: share temporary staging for oversized image uploads (src/graphics/host_gpu/renderer/cache/bufferCache.cpp)
- `d70cbd4184` shader: bound descriptor worklists by their finite bit masks (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `7e884f55e0` shader: reuse dominating descriptor loads across image samples (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `2f88a0a3b2` shader: elide bitcasts between identical physical types (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h)
- `c9ec82e32e` shader: factor live execution guards before resource planning (depends on skipped src/graphics/shader/recompiler/ShaderRecompiler.cpp, src/graphics/shader/recompiler/ir/pass)
- `1fa79d5cac` shader: specialize bounded pointer and packed image tables (depends on skipped src/graphics/shader/recompiler/ir/passes/ConstantPropagation.cpp, src/graphics/shader/recom)
- `9a3aca963a` shader: combine image execution masks with descriptor branch bounds (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `40692dfb64` graphics: copy sparse buffer uploads through resident mappings (depends on skipped src/graphics/host_gpu/renderer/cache/bufferCache.cpp, src/kernel/memory.cpp, src/kernel/mem)
- `31b7be9ff2` graphics: materialize fast clears before sampling reused targets (depends on skipped src/graphics/host_gpu/renderer/colorRenderTarget.cpp, src/graphics/host_gpu/renderer/image/)
- `717c54e085` gpu: match complete NOP marker tags and ignore opaque payloads (depends on skipped src/graphics/guest_gpu/command_processor/pm4Handlers.cpp)
- `50595d1c13` shader: recover counted image rows through private LDS spills (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `34893dd8c8` shader: recognize unconditional predicates in implication proofs (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `5f6dc0a8d0` shader: stream tile bitmaps within their fixed descriptor domain (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `ff6dffe298` graphics: decode packed unsigned float CMASK register clears (depends on skipped src/graphics/host_gpu/renderer/image/imageInfo.h)
- `3c9da9c3f4` graphics: decode both channels of RG32 float metadata clears (depends on skipped src/graphics/host_gpu/renderer/image/imageInfo.h)
- `4e16234d32` shader: bound signed image loops from their initial descriptor index (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `19799b3079` shader: simplify invariant execution masks in descriptor proofs (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `6309ac838d` shader: restrict shared-list descriptor bounds to overlapping producers (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `fe6499b86e` shader: preserve clamped scalar material selector provenance (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `907c65f54c` shader: track material offsets scaled after lane selection (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `d08a6cef64` shader: specialize guarded packed material selectors (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `f1577a5a94` shader: memoize bounded material record bit analysis (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `259a40abfc` memory: honor sparse AMM unmaps and restore failed GPU ranges (depends on skipped src/kernel/memory.cpp, src/kernel/memory.h, src/libs/libAmpr.cpp)
- `9ade7e3582` shader: prove inactive resources across masked traversal merges (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `735a229236` shader: preserve native types in indirect image tables (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `f57d2c0965` shader: keep unbounded material selectors on bounded GPU heaps (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `01ce973882` renderer: preserve guest writes to reused metadata buffers (depends on skipped src/graphics/host_gpu/renderer/render.h, src/graphics/host_gpu/renderer/renderCompute.cpp)
- `fa0e81bfba` shader: zero out-of-range scalar descriptor reads (depends on skipped src/graphics/shader/recompiler/ir/passes/SrtWalker.cpp)
- `e998f21e71` shader: reuse EXEC proof contexts across shared predicate graphs (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/SrtW)
- `9e41d8320a` shader: support returning 64-bit LDS max and compare-exchange (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h, src/graphics/shade)
- `be5915c248` graphics: skip GPU-clean empty indirect dispatches before shader preparation (depends on skipped src/graphics/host_gpu/renderer/renderCompute.cpp)
- `8e4c3b5e14` shader: prove finite masked selectors for writable buffer tables (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `1905b28b3c` shader: decode packed UNorm image loads through shared conversion (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `6abfe3bbe4` shader: share formatted loads and support packed D16 XYZW buffers (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `8d18be71db` shader: decode upper-half byte stores through shared operand selection (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `7bba785963` shader: load bytes and halfwords from GPU-selected buffers (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `4cf5a96b79` shader: reuse prepared binding offsets and shared resource switch (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp)
- `26d326d1b0` shader: share bounded record descriptors for packed loads and byte stores (depends on skipped src/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp, src/graphics/shader/recompil)
- `280b87adec` shader: allow reverse left shifts to use shared SDWA partial writes (depends on skipped src/graphics/shader/recompiler/frontend/decode/VectorAluOps.cpp)
- `150bcb06a9` shader: share runtime buffer format conversion across loads (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h, src/graphics/shader/re)
- `9cc0bba80b` shader: bound GPU-selected record descriptors by scalar table geometry (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `4b549b9cb3` shader: prove constant post-tested image selector loops (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `f3c017b168` shader: fold reads through empty scalar descriptor tables (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `c88f05f7b5` graphics: share signed storage conversion and support R16 image writes (depends on skipped src/graphics/host_gpu/renderer/pipeline/descriptors.cpp, src/graphics/shader/recompiler/bac)
- `8c7fe539b8` graphics: represent rebased contiguous mip-tail texture views (depends on skipped src/graphics/host_gpu/renderer/image/imageInfo.cpp, src/graphics/host_gpu/renderer/image/im)
- `3a925efddf` graphics: preserve sparse spans in rebased mip-tail views (depends on skipped src/graphics/host_gpu/renderer/image/imageInfo.cpp)
- `487bbf95fd` shader: share nested pointer-table image and sampler selection (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `04cb4c6553` shader: separate exact descriptor row bounds from pointer offsets (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `6d6da57a95` shader: share formatted stores for GPU-selected signed byte records (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `7c9da0e56f` shader: share finite texture selection through masked first-lane reads (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `892e70c8a1` graphics: bind unbound textures as native null descriptors (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gpu/renderer/cache)
- `302240da70` shader: lower complete wave minima for sparse fragment subgroups (depends on skipped src/graphics/shader/recompiler/ShaderRecompiler.cpp, src/graphics/shader/recompiler/backend)
- `e0586cc85a` shader: decode unsigned byte loads into preserved low halves (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `127cf8d46f` shader: route image stores through shared indirect descriptor access (depends on skipped src/graphics/presentation/window/vulkanWindow.cpp, src/graphics/shader/recompiler/backend/s)
- `b6da068313` shader: preserve typed candidates for unfiltered indirect image access (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `7468e4f60a` shader: share descriptor dispatch for scalar and wide buffer stores (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp)
- `c914435f20` shader: plan finite GPU-selected RGBA16 buffer stores (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `4843d8d942` shader: support packed D16 XYZW formatted buffer stores (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterMemory.cpp, src/graphics/shader/re)
- `82470c7181` shader: share workgroup and selector count resolution (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `ceccd746d8` shader: resolve workgroup-selected descriptor prefixes from dispatch arguments (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/host_gpu/renderer/r)
- `5313181ecf` shader: bound workgroup selector prefixes from runtime starting indices (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `2f6ee0e91d` shader: support RDNA2 high-half short buffer stores (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `b24ce3440d` shader: track buffer liveness and offsets by resource count (depends on skipped src/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp, src/graphics/shader/recompil)
- `1039c54f5d` shader: share indirect buffer array access after an empty root (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h, src/graphics/shader/re)
- `8b81f665a2` shader: separate indirect buffer candidates from tracked root limits (depends on skipped src/graphics/host_gpu/renderer/pipeline/shaders.cpp, src/graphics/shader/recompiler/backend)
- `bc2664cefa` shader: decode comparison gathers with explicit LOD (depends on skipped src/graphics/shader/recompiler/frontend/decode/ImageOps.cpp, src/graphics/shader/recompiler)
- `dd429ef858` shader: decode high-half formatted buffer stores (depends on skipped src/graphics/shader/recompiler/frontend/decode/MemoryOps.cpp, src/graphics/shader/recompile)
- `dcc642551f` graphics: classify signed byte textures for native sampling (depends on skipped src/graphics/guest_gpu/gpu_format.cpp)
- `6455546f26` shader: admit bounded multi-component formatted buffer loads (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `c1f6838e40` shader: share repeated raw indirect buffer load bodies (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h, src/graphics/shader/re)
- `50844badbf` shader: map debug SPIR-V blocks to native RDNA2 PCs (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/shader/recompiler/S)
- `bb2d6dbeb3` shader: share scalar condition results with direct subgroup votes (depends on skipped src/graphics/presentation/window/vulkanWindow.cpp, src/graphics/shader/recompiler/backend/s)
- `65a6f30f51` shader: compare real nested descriptor pointer instructions (depends on skipped src/graphics/shader/recompiler/ir/Value.h, src/graphics/shader/recompiler/ir/passes/Resourc)
- `12c522bba5` graphics: log cached GPU completion beside dispatch submission ticks (depends on skipped src/graphics/host_gpu/renderer/renderCompute.cpp)
- `279bc95941` graphics: track HTile clears across PS5 depth array slices (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gpu/renderer/cache)
- `28766a915b` audio: decode PS5 ATRAC9 discrete multichannel streams (depends on skipped src/libs/ngs2.cpp)
- `1d3fd07fe6` shader: share indirect buffer format geometry lookup (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h, src/graphics/shader/re)
- `7d24972b8f` shader: report rejected indirect buffer candidate context (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `81169d77ea` shader: materialize compact image and paired sampler tables (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `fe23334ff0` shader: remove unreachable candidate diagnostic branches (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `fcab4e5d0d` shader: trace unsupported sampled image candidate provenance (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `5916a184cc` shader: preserve affine image heap candidate domains (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `9c04abacc5` shader: resolve shared selector keys once per materialization (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `2a4ccaa5d5` shader: share proven selector domains for paired images and samplers (depends on skipped src/graphics/shader/recompiler/ir/Program.cpp, src/graphics/shader/recompiler/ir/ShaderIR.h)
- `87d938139d` graphics: log complete per-target state before format resolution (depends on skipped src/graphics/host_gpu/renderer/debug.cpp)
- `fb213e8ae5` shader: share packed color component encoding (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterImage.cpp, src/graphics/shader/rec)
- `b4406b0467` graphics: render packed 11-11-10 UNorm targets with exact rounding (depends on skipped src/graphics/guest_gpu/gpu_format.cpp, src/graphics/host_gpu/renderer/image/textureCommon.c)
- `232e2f2593` shader: bound waterfall buffer descriptors with same-lane guards (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `959bee4799` graphics: checkpoint live Vulkan pipeline cache after new compilations (depends on skipped src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp, src/graphics/host_gpu/renderer/p)
- `a50e4de389` shader: recognize reversed unsigned greater-than resource bounds (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `2285a25182` shader: prove narrowing loop masks through shared implication analysis (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `e291589177` shader: resolve GPU-selected parent pointers and bounded child image tables (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `df92a08990` shader: report descriptor materialization rejection context (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp)
- `2caf2f7e4e` shader: retain runtime count bounds for finite descriptor choices (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `bcf13e101e` shader: validate texture mip ranges and channel selectors (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/s)
- `d172ab75ca` shader: reuse selector domains proven by prior resource accesses (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `f7226ca7dc` graphics: emit PS5 register object and primitive IDs to RT7 (depends on skipped src/graphics/guest_gpu/command_processor/pm4Handlers.cpp, src/graphics/host_gpu/renderer/pi)
- `e4019af522` shader: remove disabled legacy stage register checks (depends on skipped src/graphics/shader/shader.cpp)
- `c8caa27936` shader: preserve folded native constants for descriptor provenance (depends on skipped src/graphics/shader/recompiler/ShaderRecompiler.cpp, src/graphics/shader/recompiler/fronten)
- `a5ceb48184` agc: encode acquire-memory byte ranges in standard packets (depends on skipped src/graphics/guest_gpu/command_processor/pm4Handlers.cpp, src/graphics/guest_gpu/pm4.cpp, s)
- `30a4673c60` libs: port entitlement requests and fixed-refresh VRR status (depends on skipped src/graphics/presentation/videoOut.cpp)
- `f7ae86c982` kernel: optimize virtual range edits with an ordered map (depends on skipped src/kernel/memory.cpp)
- `56d3ce98f0` kernel: implement path-based truncate for replay files (depends on skipped src/kernel/fileSystem.cpp, src/kernel/fileSystem.h, src/libs/libKernel.cpp)
- `54abbf48f2` kernel: implement vectored writes for profile index saves (depends on skipped src/kernel/fileSystem.cpp, src/kernel/fileSystem.h, src/libs/libKernel.cpp)
- `57c4a6ee11` Make trophy toast duration configurable (5-10s, default 6s) (#1538) (depends on skipped src/common/emulatorConfig.cpp, src/common/emulatorConfig.h, src/graphics/presentation/syste)
- `6568fe9a84` Implement Missing NGS2 System Functions Lock & Unlock (#1542) (depends on skipped src/libs/ngs2.cpp)
- `4242ec5a64` Add add-on (DLC) support (#1519) (depends on skipped src/emulator.cpp, src/emulator.h, src/launcher/include/configuration.h)
- `13e4c315b6` agc: Cleanup names so they match NIDs (#1508) (depends on skipped src/libs/agc.cpp, src/libs/agc.h, src/libs/libAgcDriver.cpp)
- `83db2efec3` shader: pack v_cvt_pkrtz_f16_f32 with the native half conversion (GTA V) (#1520) (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp, src/graphics/shader/recom)
- `6a0a666a41` loader: keep the output and log of a game LoadExec relaunches (#1480) (depends on skipped src/loader/loadExec.cpp)
- `3ff6606fcb` Input mapping tabs UI, emulated touchpad, and rumble for non-PS5 controllers (#1541) (depends on skipped .github/workflows/build.yml, CMakeLists.txt, src/graphics/presentation/systemOverlay.cpp)
- `57b161019d` shader: honor front-face input format (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp, src/graphics/shader/shad)
- `717b50155a` launcher: rename executable to kyty_launcher (depends on skipped .github/workflows/build.yml, README.md, src/launcher/CMakeLists.txt)
- `584c2e8ca6` Play snd0.at9 during the startup splash screen (#1545) (depends on skipped src/launcher/CMakeLists.txt, src/launcher/src/mainDialog.cpp)
- `4c6be6cd1d` Localize trophy notifications with embedded JSON catalogs (depends on skipped src/graphics/presentation/systemOverlay.cpp)
- `d58aefde49` renderer: reduce synchronization and allocation overhead (depends on skipped src/graphics/host_gpu/memoryTracker.h, src/graphics/host_gpu/renderer/cache/bufferCache.cpp)
- `b899717900` renderer: log unsupported depth feedback once (depends on skipped src/graphics/host_gpu/renderer/renderDraw.cpp)
- `d7974dc763` Fix scalar buffer reads across sparse guest mappings (depends on skipped src/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp, src/graphics/shader/recompil)
- `e7ac242039` Load packaged system modules through the native module lifecycle (depends on skipped src/libs/libKernel.cpp)
- `d01633e18e` Revert "Fix scalar buffer reads across sparse guest mappings" (depends on skipped src/graphics/shader/recompiler/backend/spirv/SpirvEmitter.cpp, src/graphics/shader/recompil)
- `4f1f3b1bb3` shader: make the f32 clamp modifier turn NaN into 0 (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp)
- `02ead0e2e1` shader: 64-bit buffer atomic add (BUFFER_ATOMIC_ADD_X2) (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h, src/graphics/shade)
- `9202bddad2` recognize verified pattern fills (depends on skipped src/graphics/host_gpu/renderer/renderCompute.cpp, src/graphics/shader/recompiler/ir/ShaderI)
- `0fd41ae206` Fix Linux CI xxHash include path (#1580) (depends on skipped CMakeLists.txt)
- `e93b4b0378` shader: support returning 32-bit LDS compare-exchange (DS_CMPST_RTN_B32) (#1578) (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h, src/graphics/shade)
- `b95ea4bb5f` Support mountable-content DLC (addcont with real content) (#1581) (depends on skipped src/kernel/fileSystem.cpp, src/libs/libAmpr.cpp, src/libs/libAppContent.cpp)
- `b6bd9ecae4` shader: read float inline constants on 16-bit operands as half bits (#1582) (depends on skipped src/graphics/shader/recompiler/frontend/translate/Translate.cpp)
- `ba8b0c85b8` shader: keep quiet NaNs with payload bits out of the packed f16 min/max sNaN path (#1579) (depends on skipped src/graphics/shader/recompiler/frontend/translate/Float.cpp)
- `b82512be06` shader: read float inline constants on 64-bit operands as f64 bits (#1576) (depends on skipped src/graphics/shader/recompiler/frontend/translate/Translate.cpp)
- `f6f1ac8b40` shader: batch dependent folds to avoid repeated SSA graph walks (depends on skipped src/graphics/shader/recompiler/ir/passes/ConstantPropagation.cpp)
- `ec08ea197b` shader: new fill pattern (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceTracking.cpp)
- `ae25558321` shader: size NGG primitive outputs from amplification (depends on skipped src/graphics/shader/shader.cpp)
- `b485dd24be` system service: stub (depends on skipped src/libs/libSystemService.cpp)
- `d1fe1324c8` shader: keep coherent 64bit atomic (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterInternal.h, src/graphics/shader/re)
- `57791645e4` renderer: track attachment load clears by image aspect (depends on skipped src/graphics/host_gpu/renderer/depthRenderTarget.cpp, src/graphics/host_gpu/renderer/depthR)
- `5bf37f52ad` renderer: clear compressed stencil with pending depth metadata (depends on skipped src/graphics/host_gpu/renderer/depthRenderTarget.cpp, src/graphics/host_gpu/renderer/render)
- `3ecc6b7d1b` network: implement POSIX socket shutdown on Linux (depends on skipped src/libs/libKernel.cpp, src/libs/network.cpp)
- `56d03f16e9` network: UDPP2P (depends on skipped src/libs/network.cpp)
- `c2173801d4` shader: skip resources in proven empty unsigned loops (depends on skipped src/graphics/shader/recompiler/ir/passes/ResourceMaterialization.cpp, src/graphics/shader/r)
- `9f88043ebe` shader: use compact integer RTZ half conversion (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterAlu.cpp)
- `890393fbe6` vulkan: start without fragmentShaderBarycentric on GPUs that lack it (#1590) (depends on skipped src/graphics/presentation/window/vulkanWindow.cpp)
- `415387f00d` Support multiple controllers for local multiplayer (#1583) (depends on skipped assets/localization/ar-AE.json, assets/localization/cs-CZ.json, assets/localization/da-DK.j)
- `6aa3846fb2` feat: add an in-game performance panel (#1090) (depends on skipped src/common/CMakeLists.txt, src/common/emulatorConfig.cpp, src/common/emulatorConfig.h)
- `d8c4a9af38` graphics: preserve HTILE metadata across native image replacement (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp)
- `763d3be3a9` graphics: add opt-in ZMask support and launcher rendering options (depends on skipped src/common/emulatorConfig.cpp, src/common/emulatorConfig.h, src/graphics/host_gpu/renderer/)
- `c3e7ec8285` graphics: fix Jusant regression from d8c4a9af3 (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp)
- `fe3857a171` graphics: materialize CMASK clears for multisampled targets (PPSA13794) (depends on skipped src/graphics/host_gpu/renderer/cache/textureCache.cpp, src/graphics/host_gpu/renderer/cache)
- `4059179e9d` graphics: write proven compute buffer fills to guest memory (#1628) (depends on skipped src/graphics/host_gpu/renderer/render.h, src/graphics/host_gpu/renderer/renderCompute.cpp)
- `33208a545a` shader: remove artificial sampled image and sampler pair limit (depends on skipped src/graphics/shader/recompiler/ir/ShaderIR.h, src/graphics/shader/recompiler/ir/passes/Reso)
- `37df7f3278` Show pipeline cache sizes in MB and remove obsolete texture folder cleanup (depends on skipped src/emulator.cpp, src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp)
- `22ef16a044` shader: implement PS5 vector data cache invalidation (depends on skipped src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp, src/graphics/shader/reco)
- `f4bdb52922` agc: restore verified fused shader size name (depends on skipped src/libs/agc.cpp, src/libs/agc.h, src/libs/libAgcDriver.cpp)
- `099de8dea6` graphics: publish each PM4 WRITE_DATA word exactly once (depends on skipped src/graphics/guest_gpu/graphicsRun.cpp)
