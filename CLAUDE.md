# CLAUDE.md — KytyPS5-UFC fine-tune pass (before Sol loop)

You are doing PASS 1 (fine-tune). Sol + OpenCode will do PASS 2 (unattended loop). Do not set up the infinite loop. Make the loop reliable.

## Project
KytyPS5 fork, C++20, Clang-only (`clang-cl` on Windows, `clang++` on Linux), Vulkan 1.3, AMD RDNA2 -> SPIR-V shader recompiler. Windows is primary target, do not regress it.

Key dirs:
- `src/graphics/shader/recompiler` — decode, IR, SPIR-V emission. Keep aligned with RDNA2 ISA + Vulkan validation.
- `src/graphics/guest_gpu` — Prospero GPU formats, command processor
- `src/graphics/host_gpu` — Vulkan backend, renderer/cache, `pageManager.cpp`, `memoryTracker.cpp`
- `src/common/profiler.h` — Tracy integration, `--profile` flag in `src/main.cpp:79`
- `tests/` — ~30 regression executables, run via `ctest`
- Automation: `MenuDrive.py`, `AutoFight.ps1`, `Capture-UFC5.ps1`, output in `_Build/autolab/<label-date>/`

## Upstream AI policy (must follow)
From `README.md:369`: AI allowed for research/dev assistance only. Human must understand, review, test all submitted code. PR descriptions/comments must be human-written. Disclose AI scope + tests in PR. Unverified changes get closed. Never auto-push, never rewrite this policy.

## Build (Windows x64 Native Tools, clang-cl)
```powershell
git submodule update --init --recursive
cmake -S . -B _Build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
cmake --build _Build/windows --target launcher
cmake --install _Build/windows --prefix _Build/windows/install
```

## Test
```powershell
cmake --build _Build/windows --target kyty_tests
ctest --test-dir _Build/windows --output-on-failure
```
Also: `pre-commit` clang-format for `src/**/*.cpp,*.h,*.inc`. Clang-tidy on `kyty_emulator`.

## Automation harness (what you are tuning)
1. `MenuDrive.py` — virtual DS4 (ViGEmBus + vgamepad). `python MenuDrive.py --button cross --count 7 --interval 2.5 --hold 0.25 --log-csv presses.csv` — logs `press,elapsed_s` + `total`.
2. `AutoFight.ps1` — launches `kyty_emulator.exe --game <dir-with-eboot.bin> --profile`, captures `console.log`, `stderr.log`, `metrics.csv (t,fps,cpu,rss,gpu,brightness)`, `shot_*.png`, `fight.tracy`, `summary.txt`.
3. `Capture-UFC5.ps1 -Seconds 30` — manual Tracy capture, requires `_Build/tracy-capture/tracy-capture.exe`.

Fight-enter signal: first `fps>0` + `brightness>12` in `metrics.csv`, confirmed by `shot_*.png`.

## Your fine-tune tasks (only these)
1. Make `AutoFight.ps1` + `MenuDrive.py` robust: handle emu crash/exit early, missing `tracy-capture.exe`, missing `nvidia-smi`, `MainWindowHandle==0`. No hardcoded `C:\Users\dalda\` paths — use params.
2. Add console-log triage: parse `console.log` for `[FATAL]/[ERROR]/Vulkan validation/SPIR-V` lines, write `triage.md` with top 5 errors + file:line hints. Do not fix shader semantics yet — just classify.
3. Calibrate defaults: set `MenuDrive` default to `7x cross, 2.5s interval, 0.25s hold` and `AutoFight` `MonitorSeconds=120, ScreenshotEverySec=5`. Document how to sweep `2s/2.5s/3s/4s` using `metrics.csv`.
4. Ensure `tracy-csvexport` output (if present in `_Build/tracy-csvexport`) is wired into `summary.txt` when available, silently skipped when not.

## Rules
- Small focused diffs. Do not touch guest JIT entitlements, linker `guest_address_space`, or shader IR passes unless asked.
- Every change must still build on Windows + pass `kyty_tests`.
- Leave clear `TODO(Sol-loop):` markers where the unattended Sol loop should take over (log watch -> rebuild -> retest).
- Output at end: what you changed, build+test evidence, recommended X-press interval, and handoff notes for Sol.
