#ifndef EMULATOR_SRC_COMMON_CPUAFFINITY_H_
#define EMULATOR_SRC_COMMON_CPUAFFINITY_H_

#include <chrono>
#include <cstdint>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace Common {

// On a CPU with cores of different speeds (Intel 12th-14th gen and later hybrid designs), keeps
// the calling thread on the fastest cores: the emulator's GPU thread is its bottleneck, and on an
// efficiency core it ran ~1.7x slower per operation (i7-13700F on Linux, 2026-10-06). Windows:
// CPU sets of the highest efficiency class (a scheduler preference). Linux: the cores with the
// highest maximum frequency. A no-op on CPUs whose cores are all alike, on macOS, and with
// KYTY_PCORE_AFFINITY=0. `name` is for the log line.
void PreferPerformanceCores(const char* name);

// The emulated GPU's own threads (the GPU thread, the draw-prep scanner, the Vulkan recording and
// submission threads) hand work to each other many times a frame. Two opt-in tunings for them,
// after TheCruZ/KytyPS5-GTA 12bb591a (both off by default):
// - KYTY_GPU_THREAD_PRIORITY=1 raises the calling thread to THREAD_PRIORITY_HIGHEST (Windows only),
//   so the game's own threads do not preempt it. `name` is for the log line.
// - KYTY_GPU_SPIN_US=<n> lets a handoff spin for up to n microseconds (SpinUntil) before it sleeps
//   on its condition variable, so a busy frame does not pay a kernel wake-up per handoff.
void RaiseGpuThreadPriority(const char* name);
[[nodiscard]] uint32_t GpuHandoffSpinMicros();

// Spins until ready() or `micros` have passed, pausing longer each round; returns ready().
template <typename Ready>
bool SpinUntil(Ready&& ready, uint32_t micros) {
	if (micros == 0) {
		return ready();
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
	for (uint32_t pauses = 1;; pauses = pauses < 64u ? pauses * 2u : pauses) {
		if (ready()) {
			return true;
		}
		if (std::chrono::steady_clock::now() >= deadline) {
			return ready();
		}
		for (uint32_t i = 0; i < pauses; i++) {
#if defined(_M_X64) || defined(__x86_64__)
			_mm_pause();
#endif
		}
	}
}

} // namespace Common

#endif // EMULATOR_SRC_COMMON_CPUAFFINITY_H_
