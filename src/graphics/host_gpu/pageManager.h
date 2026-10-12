#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_

#include "common/common.h"
#include "graphics/host_gpu/regionDefinitions.h"

#include <memory>

namespace Libs::Graphics {

enum class PageFaultAccess { Read, Write, Execute, Unknown };

class PageManager final {
public:
	// The owners of a page's protection: cache tracking of CPU writes (read-only pages), buffers
	// the GPU wrote (no access, one owner per page) and GPU-written images whose guest bytes are
	// stale (no access, any number of owners per page).
	enum class WatchKind : uint8_t { Write, BufferRead, ImageRead };

	struct PageWatchers {
		uint32_t write        = 0;
		uint32_t buffer_read  = 0;
		uint32_t image_read   = 0;
	};

	PageManager();
	// The owner must stop all PageManager callers before destruction.
	~PageManager();

	KYTY_CLASS_NO_COPY(PageManager);

	[[nodiscard]] uint64_t GetPageSize() const;
	// Whether cache tracking currently restricts access to the page holding vaddr.
	[[nodiscard]] bool IsWatched(uint64_t vaddr) const;
	// The owners of the page holding vaddr.
	[[nodiscard]] PageWatchers Watchers(uint64_t vaddr) const;
	// Whether a page of the range may be read protected for a GPU-written image, without taking a
	// lock: false means no page of the range was when the call started.
	[[nodiscard]] bool MayHaveImageReadWatch(uint64_t vaddr, uint64_t size) const noexcept;
	// The protection changes of all page managers so far (address-space protection calls).
	[[nodiscard]] static uint64_t ProtectionCalls() noexcept;
	// Those that changed the protection for image read watchers.
	[[nodiscard]] static uint64_t ImageProtectionCalls() noexcept;

	template <bool track>
	void UpdatePageWatchers(uint64_t vaddr, uint64_t size);
	template <bool track, bool is_read = false>
	void UpdatePageWatchersForRegion(uint64_t base_addr, RegionBits& mask);
	template <bool track>
	void UpdateImageReadWatchers(uint64_t vaddr, uint64_t size);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_PAGEMANAGER_H_
