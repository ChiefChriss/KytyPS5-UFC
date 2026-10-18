#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_VRAMBUDGET_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_VRAMBUDGET_H_

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// KYTY_UFC_VRAM_GC_BUDGET=1 (opt-in, default off): the buffer and texture caches collect against
// the device memory budget as it is now (re-read once per guest frame), with images aged in frames,
// instead of marks fixed at startup and ages counted in GC ticks. Minimal port of Jetsku's
// KYTY_VRAM_GC_BUDGET (src/graphics/host_gpu/vramBudget.h in Jetsku/KytyPS5).
//
// The stock marks are computed once at startup from min(budget, 8 GiB); on cards below ~16 GB both
// collectors run all the time and re-create/re-upload what every frame uses. With the switch on,
// B = GraphicContext::GetTotalMemoryBudget() (VK_EXT_memory_budget budget minus a reserve) is read
// once per frame and:
//   - images: above ImageTrigger(B) = 3/4 B, once per frame and oldest first, clean images (not
//     GPU-modified, not a depth/stencil pair, not bound, not a render target, not a video-out
//     surface) unused for AgeFrames() frames (a quarter of that, at least 4, at or above
//     Critical(B) = 15/16 B) are freed until usage is back at the trigger. No download, no GPU
//     wait. If usage is still at or above B afterwards, the stock image collector runs as the last
//     resort.
//   - buffers: the stock buffer collector (with its BDA keep / hot-range logic) runs only above B,
//     and its aggressive path (downloads of GPU-dirty buffers, GPU wait) only above B + B/16.
// KYTY_UFC_VRAM_GC_AGE_FRAMES=<n> sets the image age (default 30 frames).
// Without VK_EXT_memory_budget (CanReportMemoryUsage() false) the stock collectors run unchanged.
namespace Libs::Graphics::VramBudget {

[[nodiscard]] inline bool GcEnabled() noexcept {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_UFC_VRAM_GC_BUDGET"); // Dev 094 all-on bisect: on by default, =0 turns it off
		return value == nullptr || std::strcmp(value, "0") != 0;
	}();
	return enabled;
}

constexpr uint64_t DefaultAgeFrames = 30;

[[nodiscard]] inline uint64_t AgeFrames() noexcept {
	static const uint64_t frames = [] {
		const char* value = std::getenv("KYTY_UFC_VRAM_GC_AGE_FRAMES");
		if (value == nullptr || value[0] == '\0') {
			return DefaultAgeFrames;
		}
		char*      end    = nullptr;
		const auto parsed = std::strtoull(value, &end, 10);
		return end != value && *end == '\0' && parsed != 0 ? static_cast<uint64_t>(parsed)
		                                                   : DefaultAgeFrames;
	}();
	return frames;
}

[[nodiscard]] constexpr uint64_t ImageTrigger(uint64_t budget) noexcept {
	return budget / 4 * 3;
}

[[nodiscard]] constexpr uint64_t Critical(uint64_t budget) noexcept {
	return budget - budget / 16;
}

[[nodiscard]] constexpr uint64_t BufferTrigger(uint64_t budget) noexcept {
	return budget;
}

[[nodiscard]] constexpr uint64_t BufferCritical(uint64_t budget) noexcept {
	return budget + budget / 16;
}

struct ImagePlan {
	bool     retire     = false;
	uint64_t age_frames = 0; // free clean images unused for more than this many frames
	uint64_t bytes      = 0; // until this many bytes are freed
};

// What the image collector does this frame at `usage` of planning budget `budget`.
[[nodiscard]] constexpr ImagePlan PlanImages(uint64_t budget, uint64_t usage,
                                             uint64_t age_frames) noexcept {
	const auto trigger = ImageTrigger(budget);
	if (budget == 0 || usage <= trigger) {
		return {};
	}
	const auto age = age_frames != 0 ? age_frames : DefaultAgeFrames;
	return {true, usage >= Critical(budget) ? std::max<uint64_t>(age / 4, 4) : age,
	        usage - trigger};
}

constexpr uint64_t GiB = 1024ull * 1024 * 1024;
static_assert(!PlanImages(0, 5 * GiB, 30).retire);
static_assert(!PlanImages(8 * GiB, 6 * GiB, 30).retire);
static_assert(PlanImages(8 * GiB, 7 * GiB, 30).retire &&
              PlanImages(8 * GiB, 7 * GiB, 30).age_frames == 30 &&
              PlanImages(8 * GiB, 7 * GiB, 30).bytes == 1 * GiB);
static_assert(PlanImages(8 * GiB, 8 * GiB, 30).age_frames == 7);
static_assert(PlanImages(8 * GiB, 8 * GiB, 8).age_frames == 4);

} // namespace Libs::Graphics::VramBudget

#endif /* EMULATOR_SRC_GRAPHICS_HOST_GPU_VRAMBUDGET_H_ */
