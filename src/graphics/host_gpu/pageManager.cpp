#include "graphics/host_gpu/pageManager.h"

#include "common/alignment.h"
#include "common/virtualMemory.h"
#include "graphics/host_gpu/regionDefinitions.h"
#include "kernel/memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__)
#include <xmmintrin.h>
#endif

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef min
#undef max
#else
#include <unistd.h>
#endif

namespace Libs::Graphics {
namespace {

constexpr uint64_t PAGE_SIZE    = TRACKER_PAGE_SIZE;
constexpr uint64_t REGION_SIZE  = TRACKER_REGION_SIZE;
constexpr uint64_t ADDRESS_SIZE = TRACKER_ADDRESS_SIZE;
constexpr uint64_t REGION_COUNT = ADDRESS_SIZE / REGION_SIZE;

constexpr uint64_t REGION_PAGES = REGION_SIZE / PAGE_SIZE;

[[noreturn]] void FailFast(const char* reason = nullptr) noexcept {
	std::fputs("PageManager fail-fast: ", stderr);
	std::fputs(reason != nullptr ? reason : "invalid page state", stderr);
	std::fputc('\n', stderr);
	std::fflush(stderr);
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(EXCEPTION_NONCONTINUABLE_EXCEPTION));
#endif
	std::_Exit(322);
}

[[noreturn]] void Fatal(const char* format, ...) {
	std::fputs("PageManager fatal: ", stderr);
	va_list args;
	va_start(args, format);
	std::vfprintf(stderr, format, args);
	va_end(args);
	std::fputc('\n', stderr);
	std::fflush(stderr);
	std::_Exit(322);
}

class SpinGuard final {
public:
	explicit SpinGuard(std::atomic_flag& lock): m_lock(lock) {
		while (m_lock.test_and_set(std::memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
			_mm_pause();
#elif defined(_M_ARM64)
			__yield();
#elif defined(__aarch64__)
			asm volatile("yield");
#endif
		}
	}
	~SpinGuard() { m_lock.clear(std::memory_order_release); }
	KYTY_CLASS_NO_COPY(SpinGuard);

private:
	std::atomic_flag& m_lock;
};

void ValidateRange(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		Fatal("invalid range vaddr=0x%016" PRIx64 ", size=0x%016" PRIx64, vaddr, size);
	}
}

using WatchKind = PageManager::WatchKind;

// Address-space protection changes, all and those of image read watchers, for statistics.
std::atomic<uint64_t> g_protection_calls {0};
std::atomic<uint64_t> g_image_protection_calls {0};

} // namespace

struct PageManager::Impl {
	struct PageState {
		uint8_t write_watchers  : 7 = 0;
		uint8_t access_watchers : 1 = 0;
		// GPU-written images whose guest bytes on the page are stale: any number may share a page.
		uint8_t image_read_watchers = 0;

		[[nodiscard]] Common::VirtualMemory::Mode Perms() const noexcept {
			if (access_watchers != 0 || image_read_watchers != 0) {
				return Common::VirtualMemory::Mode::NoAccess;
			}
			if (write_watchers != 0) {
				return Common::VirtualMemory::Mode::Read;
			}
			return Common::VirtualMemory::Mode::ReadWrite;
		}

		[[nodiscard]] bool Watched() const noexcept {
			return write_watchers != 0 || access_watchers != 0 || image_read_watchers != 0;
		}

		template <int delta, WatchKind kind>
		uint32_t AddDelta(uint64_t address) {
			static_assert(delta >= -1 && delta <= 1);
			if constexpr (kind == WatchKind::BufferRead) {
				if constexpr (delta == 1) {
					if (access_watchers != 0) {
						Fatal("read-watcher overflow at 0x%016" PRIx64, address);
					}
					return ++access_watchers;
				} else if constexpr (delta == -1) {
					if (access_watchers == 0) {
						Fatal("read-watcher underflow at 0x%016" PRIx64, address);
					}
					return --access_watchers;
				} else {
					return access_watchers;
				}
			} else if constexpr (kind == WatchKind::ImageRead) {
				if constexpr (delta == 1) {
					if (image_read_watchers == UINT8_MAX) {
						Fatal("image read-watcher overflow at 0x%016" PRIx64, address);
					}
					return ++image_read_watchers;
				} else if constexpr (delta == -1) {
					if (image_read_watchers == 0) {
						Fatal("image read-watcher underflow at 0x%016" PRIx64, address);
					}
					return --image_read_watchers;
				} else {
					return image_read_watchers;
				}
			} else {
				if constexpr (delta == 1) {
					if (write_watchers == 0x7f) {
						Fatal("write-watcher overflow at 0x%016" PRIx64, address);
					}
					return ++write_watchers;
				} else if constexpr (delta == -1) {
					if (write_watchers == 0) {
						Fatal("write-watcher underflow at 0x%016" PRIx64, address);
					}
					return --write_watchers;
				} else {
					return write_watchers;
				}
			}
		}
	};
	static_assert(sizeof(PageState) == 2);

	static constexpr size_t IMAGE_BIT_WORDS = REGION_PAGES / 64;

	struct Region {
		std::atomic_flag                    lock = ATOMIC_FLAG_INIT;
		std::array<PageState, REGION_PAGES> pages;
		// The pages with image read watchers, written under the lock and read without it. They are
		// set before the protection is installed and cleared after it is lifted, and live on their
		// own cache lines: draws read them, only image arming writes them.
		alignas(64) std::atomic<uint32_t> image_read_pages {0};
		std::array<std::atomic<uint64_t>, IMAGE_BIT_WORDS> image_read_bits {};
	};

	Impl() {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
		SYSTEM_INFO info {};
		GetSystemInfo(&info);
		if (info.dwPageSize != PAGE_SIZE) {
			Fatal("unsupported host page size 0x%08" PRIx32,
			      static_cast<uint32_t>(info.dwPageSize));
		}
#elif defined(__APPLE__)
		// Under Rosetta the host page size is 4 KB, matching TRACKER_PAGE_SIZE.
		if (static_cast<uint64_t>(getpagesize()) != PAGE_SIZE) {
			Fatal("unsupported host page size 0x%08" PRIx32, static_cast<uint32_t>(getpagesize()));
		}
#else
		const auto host_page_size = ::sysconf(_SC_PAGESIZE);
		if (host_page_size < 0 || static_cast<uint64_t>(host_page_size) != PAGE_SIZE) {
			Fatal("unsupported host page size %ld", static_cast<long>(host_page_size));
		}
#endif
		regions = std::make_unique<std::atomic<Region*>[]>(REGION_COUNT);
	}

	~Impl() {
		for (const auto& region: region_storage) {
			SpinGuard lock(region->lock);
			for (auto& page: region->pages) {
				if (page.Watched()) {
					FailFast("PageManager destroyed with live page state");
				}
			}
		}
	}

	Region* FindRegion(uint64_t vaddr) const noexcept {
		return vaddr < ADDRESS_SIZE ? regions[vaddr / REGION_SIZE].load(std::memory_order_acquire)
		                            : nullptr;
	}

	Region* GetOrCreateRegion(uint64_t vaddr) {
		const auto index = vaddr / REGION_SIZE;
		if (auto* region = regions[index].load(std::memory_order_acquire); region != nullptr) {
			return region;
		}
		std::lock_guard lock(region_mutex);
		if (auto* region = regions[index].load(std::memory_order_acquire); region != nullptr) {
			return region;
		}
		auto  region = std::make_unique<Region>();
		auto* ptr    = region.get();
		region_storage.push_back(std::move(region));
		regions[index].store(ptr, std::memory_order_release);
		return ptr;
	}

	void Protect(uint64_t vaddr, uint64_t size, Common::VirtualMemory::Mode mode) noexcept {
		g_protection_calls.fetch_add(1, std::memory_order_relaxed);
		if (!Libs::LibKernel::Memory::ProtectGuestHostMemory(vaddr, size, mode)) {
			Fatal("address-space protection failed at 0x%016" PRIx64 ", mode=0x%08" PRIx32, vaddr,
			      static_cast<uint32_t>(mode));
		}
	}

	// Caller holds the region lock.
	void NoteImageReadPage(Region& region, size_t page_index, bool watched) noexcept {
		auto&      word = region.image_read_bits[page_index / 64];
		const auto bit  = uint64_t {1} << (page_index % 64);
		if (watched) {
			word.store(word.load(std::memory_order_relaxed) | bit, std::memory_order_release);
			if (region.image_read_pages.fetch_add(1, std::memory_order_acq_rel) == 0) {
				image_read_regions.fetch_add(1, std::memory_order_acq_rel);
			}
		} else {
			word.store(word.load(std::memory_order_relaxed) & ~bit, std::memory_order_release);
			if (region.image_read_pages.fetch_sub(1, std::memory_order_acq_rel) == 1) {
				image_read_regions.fetch_sub(1, std::memory_order_acq_rel);
			}
		}
	}

	template <bool track, WatchKind kind, bool masked>
	void UpdateRegionWatchers(Region& region, uint64_t base_addr, size_t first, size_t last,
	                          const RegionBits* mask = nullptr) {
		SpinGuard lock(region.lock);
		auto      perms                 = region.pages[first].Perms();
		uint64_t  range_begin           = 0;
		uint64_t  range_bytes           = 0;
		uint64_t  potential_range_bytes = 0;
		// Image pages whose last watcher left: their bits are cleared once access is restored.
		bool released_image_pages = false;

		const auto release_pending = [&] {
			if (range_bytes != 0) {
				if constexpr (kind == WatchKind::ImageRead) {
					g_image_protection_calls.fetch_add(1, std::memory_order_relaxed);
				}
				Protect(base_addr + range_begin * PAGE_SIZE, range_bytes, perms);
				range_bytes           = 0;
				potential_range_bytes = 0;
			}
		};

		for (size_t page_index = first; page_index < last; page_index++) {
			auto&      page    = region.pages[page_index];
			const auto address = base_addr + page_index * PAGE_SIZE;
			const bool update  = !masked || mask->Get(page_index);

			const auto old_perms = page.Perms();
			const auto new_count = update ? page.AddDelta<track ? 1 : -1, kind>(address)
			                              : page.AddDelta<0, kind>(address);
			const auto new_perms = page.Perms();

			if (new_perms != perms) [[unlikely]] {
				release_pending();
				perms = new_perms;
			} else if (range_bytes != 0) {
				potential_range_bytes += PAGE_SIZE;
			}

			if (!update) {
				continue;
			}

			const bool watcher_edge = (track && new_count == 1) || (!track && new_count == 0);
			if constexpr (kind == WatchKind::ImageRead) {
				if (watcher_edge && track) {
					NoteImageReadPage(region, page_index, true);
				}
				released_image_pages |= watcher_edge && !track;
			}
			if (watcher_edge && old_perms != new_perms) {
				if (range_bytes == 0) {
					range_begin           = page_index;
					potential_range_bytes = PAGE_SIZE;
				}
				range_bytes = potential_range_bytes;
			}
		}

		release_pending();

		if constexpr (kind == WatchKind::ImageRead && !track) {
			if (released_image_pages) {
				for (size_t page_index = first; page_index < last; page_index++) {
					auto&      word = region.image_read_bits[page_index / 64];
					const auto bit  = uint64_t {1} << (page_index % 64);
					if (region.pages[page_index].image_read_watchers == 0 &&
					    (word.load(std::memory_order_relaxed) & bit) != 0) {
						NoteImageReadPage(region, page_index, false);
					}
				}
			}
		}
	}

	template <bool track, WatchKind kind>
	void UpdatePageWatchers(uint64_t vaddr, uint64_t size) {
		ValidateRange(vaddr, size);
		const auto begin = Common::AlignDown(vaddr, PAGE_SIZE);
		const auto end   = Common::AlignUp(vaddr + size, PAGE_SIZE);
		for (auto chunk_begin = begin; chunk_begin < end;) {
			const auto chunk_end = std::min(end, Common::AlignUp(chunk_begin + 1, REGION_SIZE));
			const auto region_base = Common::AlignDown(chunk_begin, REGION_SIZE);
			auto*      region = track ? GetOrCreateRegion(chunk_begin) : FindRegion(chunk_begin);
			if (region == nullptr) {
				Fatal("untracking unknown page 0x%016" PRIx64, chunk_begin);
			}
			const auto first = static_cast<size_t>((chunk_begin - region_base) / PAGE_SIZE);
			const auto last  = static_cast<size_t>((chunk_end - region_base) / PAGE_SIZE);
			UpdateRegionWatchers<track, kind, false>(*region, region_base, first, last);
			chunk_begin = chunk_end;
		}
	}

	[[nodiscard]] bool MayHaveImageReadWatch(uint64_t vaddr, uint64_t size) const noexcept {
		if (image_read_regions.load(std::memory_order_acquire) == 0 || size == 0 ||
		    vaddr >= ADDRESS_SIZE) {
			return false;
		}
		const auto end = vaddr + std::min(size, ADDRESS_SIZE - vaddr);
		for (auto chunk_begin = vaddr; chunk_begin < end;) {
			const auto region_base = Common::AlignDown(chunk_begin, REGION_SIZE);
			const auto chunk_end   = std::min(end, region_base + REGION_SIZE);
			const auto* region     = FindRegion(chunk_begin);
			if (region != nullptr && region->image_read_pages.load(std::memory_order_acquire) != 0) {
				const auto first = static_cast<size_t>((chunk_begin - region_base) / PAGE_SIZE);
				const auto last  = static_cast<size_t>((chunk_end - 1 - region_base) / PAGE_SIZE);
				for (size_t word = first / 64; word <= last / 64; word++) {
					auto bits = region->image_read_bits[word].load(std::memory_order_acquire);
					if (word == first / 64) {
						bits &= ~uint64_t {0} << (first % 64);
					}
					if (word == last / 64 && last % 64 != 63) {
						bits &= (uint64_t {1} << (last % 64 + 1)) - 1;
					}
					if (bits != 0) {
						return true;
					}
				}
			}
			chunk_begin = chunk_end;
		}
		return false;
	}

	std::unique_ptr<std::atomic<Region*>[]> regions;
	std::vector<std::unique_ptr<Region>>    region_storage;
	std::mutex                              region_mutex;
	// Regions with image read watchers: most calls of MayHaveImageReadWatch() stop at it.
	alignas(64) std::atomic<uint64_t> image_read_regions {0};
};

static_assert(std::atomic<void*>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

PageManager::PageManager(): m_impl(std::make_unique<Impl>()) {}

PageManager::~PageManager() = default;

uint64_t PageManager::GetPageSize() const {
	return PAGE_SIZE;
}

bool PageManager::IsWatched(uint64_t vaddr) const {
	auto* region = m_impl->FindRegion(vaddr);
	if (region == nullptr) {
		return false;
	}
	const auto page = static_cast<size_t>((vaddr % REGION_SIZE) / PAGE_SIZE);
	SpinGuard  lock(region->lock);
	return region->pages[page].Watched();
}

PageManager::PageWatchers PageManager::Watchers(uint64_t vaddr) const {
	auto* region = m_impl->FindRegion(vaddr);
	if (region == nullptr) {
		return {};
	}
	const auto  page = static_cast<size_t>((vaddr % REGION_SIZE) / PAGE_SIZE);
	SpinGuard   lock(region->lock);
	const auto& state = region->pages[page];
	return {state.write_watchers, state.access_watchers, state.image_read_watchers};
}

bool PageManager::MayHaveImageReadWatch(uint64_t vaddr, uint64_t size) const noexcept {
	return m_impl->MayHaveImageReadWatch(vaddr, size);
}

uint64_t PageManager::ProtectionCalls() noexcept {
	return g_protection_calls.load(std::memory_order_relaxed);
}

uint64_t PageManager::ImageProtectionCalls() noexcept {
	return g_image_protection_calls.load(std::memory_order_relaxed);
}

template <bool track>
void PageManager::UpdatePageWatchers(uint64_t vaddr, uint64_t size) {
	m_impl->UpdatePageWatchers<track, WatchKind::Write>(vaddr, size);
}

template void PageManager::UpdatePageWatchers<true>(uint64_t, uint64_t);
template void PageManager::UpdatePageWatchers<false>(uint64_t, uint64_t);

template <bool track>
void PageManager::UpdateImageReadWatchers(uint64_t vaddr, uint64_t size) {
	m_impl->UpdatePageWatchers<track, WatchKind::ImageRead>(vaddr, size);
}

template void PageManager::UpdateImageReadWatchers<true>(uint64_t, uint64_t);
template void PageManager::UpdateImageReadWatchers<false>(uint64_t, uint64_t);

template <bool track, bool is_read>
void PageManager::UpdatePageWatchersForRegion(uint64_t base_addr, RegionBits& mask) {
	constexpr auto kind = is_read ? WatchKind::BufferRead : WatchKind::Write;
	if (base_addr % REGION_SIZE != 0 || base_addr >= ADDRESS_SIZE ||
	    REGION_SIZE > ADDRESS_SIZE - base_addr) {
		Fatal("invalid tracking region base 0x%016" PRIx64, base_addr);
	}

	const auto start_range = mask.FirstRange();
	const auto end_range   = mask.LastRange();
	if (start_range.first == REGION_PAGES) {
		FailFast("empty region watcher mask");
	}
	const auto first = start_range.first;
	const auto last  = end_range.second;
	if (start_range.second == end_range.second) {
		m_impl->UpdatePageWatchers<track, kind>(base_addr + first * PAGE_SIZE,
		                                        (last - first) * PAGE_SIZE);
		return;
	}

	auto* region = track ? m_impl->GetOrCreateRegion(base_addr) : m_impl->FindRegion(base_addr);
	if (region == nullptr) {
		Fatal("untracking unknown region 0x%016" PRIx64, base_addr);
	}
	m_impl->UpdateRegionWatchers<track, kind, true>(*region, base_addr, first, last, &mask);
}

template void PageManager::UpdatePageWatchersForRegion<true, true>(uint64_t, RegionBits&);
template void PageManager::UpdatePageWatchersForRegion<true, false>(uint64_t, RegionBits&);
template void PageManager::UpdatePageWatchersForRegion<false, true>(uint64_t, RegionBits&);
template void PageManager::UpdatePageWatchersForRegion<false, false>(uint64_t, RegionBits&);

} // namespace Libs::Graphics
