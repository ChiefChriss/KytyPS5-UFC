# Comparing original Dev 124 with the upstream integration

## Launch the builds separately

| Build | Double-click from the project root | Runtime folder under `_Build` |
| --- | --- | --- |
| Original October 7 Dev 124 | `Launch-UFC-Dev124.cmd` | `UFC-Dev124-Standalone` |
| Upstream 099de8d plus selected fork ports | `Launch-UFC-Upstream.cmd` | `UFC-Upstream-099de8d` |

The standalone old build is the preserved **original binary**, not a rebuild
of today's merged source. Its matching launcher, Qt plugins, and runtime DLLs
were copied with it. Its emulator SHA256 is
`8C7D079A65B9129BA032678EC8B1AABBAF735953A07BBC4C87F9458CEA577DE2`.

Both currently carry a Dev 124 window label. Identify them by the shortcut or
executable path; the integrated binary was built October 18, the old binary
October 7.

Each shortcut sets the working directory to its own package, keeping relative
settings, saves, downloads, and caches separate. The old package started with
a copy of the new package's launcher INI, but no save/download/cache data.
Both packages point at the same extracted UFC 5 game directory.

Verification: emulator and launcher hashes match the original installed files.
The standalone emulator's `--help` exits successfully. The copied launcher opened
a visible, input-ready window displaying `UFC Dev 124 | 971b7e2-dirty | Built
Oct 7 2026 02:00:36` and closed normally with exit code 0. No UFC fight was launched
as part of this startup check.

## A useful manual comparison

1. Close one emulator before starting the other. Keep other workloads stable.
2. Confirm the same resolution, present mode, vblank frequency, fullscreen mode,
   GPU, profiling, and validation settings in both launchers. Settings are only
   copied once; later changes are independent. The old build does not support
   every newer upstream setting.
3. Warm up each build with one fight. Do not score that run: shader compilation,
   cache population, and first-time setup can dominate it. Keep caches separate.
4. Use the same fighters, arena, match type, camera, and fixed 2–3 minute gameplay
   window. Exclude menus, walkouts, pauses, and replays.
5. Alternate runs, for example **old → new → old**, to reduce temperature and
   background-work bias. Record average FPS and visible stutters for each run.
6. Also note stretched meshes, missing textures, black frames, and hangs. A faster
   run with corruption is not a correctness improvement.

The integrated build passed 58/58 CTests before packaging. This is not a claim
that it outperforms Dev 124 in UFC, nor that either build is visually correct
throughout a fight. No new UFC performance measurements were made while preparing
these comparison launchers.
