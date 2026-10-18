#include "graphics/host_gpu/memoryTracker.h"

#include "common/alignment.h"
#include "common/assert.h"

namespace Libs::Graphics {

static_assert(std::atomic<void*>::is_always_lock_free);

MemoryTracker::MemoryTracker(PageManager& page_manager): m_page_manager(page_manager) {
	m_regions = std::make_unique<std::atomic<RegionManager*>[]>(REGION_COUNT);
}

MemoryTracker::~MemoryTracker() = default;

#if KYTY_BUILD == KYTY_BUILD_DEBUG
void MemoryTracker::ValidateGpuDirtyPages(const RangeSet& dirty, uint64_t vaddr, uint64_t size,
                                          const char* operation) const noexcept {
	if (!GuestRange {vaddr, size}.Valid() || (vaddr & (TRACKER_PAGE_SIZE - 1)) != 0 ||
	    (size & (TRACKER_PAGE_SIZE - 1)) != 0) {
		EXIT("MemoryTracker: invalid dirty-page validation range\n");
	}
	for (auto page = vaddr; page < vaddr + size; page += TRACKER_PAGE_SIZE) {
		if (!dirty.Intersects(page, TRACKER_PAGE_SIZE)) {
			EXIT("MemoryTracker: GPU-dirty tracker page has no dirty bytes, operation=%s "
			     "addr=0x%016" PRIx64 "\n",
			     operation, page);
		}
	}
}

void MemoryTracker::ValidateGpuDirtyOwnership(const RangeSet& dirty, uint64_t vaddr, uint64_t size,
                                              const char* operation) {
	ValidateRange(vaddr, size);
	const auto begin = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
	const auto end   = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
	for (auto page = begin; page < end; page += TRACKER_PAGE_SIZE) {
		const bool has_dirty_bytes = dirty.Intersects(page, TRACKER_PAGE_SIZE);
		if (IsRegionGpuModified(page, TRACKER_PAGE_SIZE) != has_dirty_bytes) {
			EXIT("MemoryTracker: tracker and byte ownership disagree, operation=%s "
			     "addr=0x%016" PRIx64 "\n",
			     operation, page);
		}
	}
}
#endif

void MemoryTracker::ValidateRange(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("invalid memory tracker range\n");
	}
}

RegionManager* MemoryTracker::GetOrCreateRegion(uint64_t index) {
	if (auto* manager = m_regions[index].load(std::memory_order_acquire); manager != nullptr) {
		return manager;
	}
	std::lock_guard lock(m_region_mutex);
	if (auto* manager = m_regions[index].load(std::memory_order_acquire); manager != nullptr) {
		return manager;
	}
	auto  manager = std::make_unique<RegionManager>(m_page_manager, index * TRACKER_REGION_SIZE);
	auto* ptr     = manager.get();
	m_region_storage.push_back(std::move(manager));
	// New regions start with every page CPU-dirty. Log before publishing the region, so no
	// thread can observe a dirty page that is not yet logged.
	RecordCpuDirty(index * TRACKER_REGION_SIZE, TRACKER_REGION_SIZE);
	m_regions[index].store(ptr, std::memory_order_release);
	return ptr;
}

void MemoryTracker::RecordCpuDirty(uint64_t vaddr, uint64_t size) noexcept {
	if (size == 0) {
		return;
	}
	// Bump before the overflow early-return so consumers can always detect that a transition
	// happened, even when this entry is dropped (the log is then treated as incomplete).
	m_cpu_dirty_generation.fetch_add(1, std::memory_order_acq_rel);
	// ChangeState dirties and unlocks whole tracker pages. Subsequent stores anywhere
	// on those pages need no new fault, so the log must cover that same footprint
	// before a consumer intersects it with its requested (possibly sub-page) ranges.
	const auto end = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
	vaddr          = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
	size           = end - vaddr;
	std::scoped_lock lock(m_cpu_dirty_lock);
	if (m_cpu_dirty_overflow) {
		return;
	}
	if (m_cpu_dirty_count != 0) {
		// Coalesce with the previous entry when contiguous or overlapping (e.g. one range
		// logged region by region).
		auto& last = m_cpu_dirty_log[m_cpu_dirty_count - 1];
		if (last.address <= vaddr && vaddr <= last.End()) {
			last.size = std::max(last.End(), vaddr + size) - last.address;
			return;
		}
	}
	if (m_cpu_dirty_count == m_cpu_dirty_log.size()) {
		m_cpu_dirty_overflow = true;
		return;
	}
	m_cpu_dirty_log[m_cpu_dirty_count++] = {vaddr, size};
}

bool MemoryTracker::TakeCpuDirtyRanges(std::vector<GuestRange>& ranges) {
	// Reserve before locking: the fault handler may spin on this lock.
	ranges.reserve(CPU_DIRTY_LOG_SIZE);
	std::scoped_lock lock(m_cpu_dirty_lock);
	const bool complete = !m_cpu_dirty_overflow;
	ranges.assign(m_cpu_dirty_log.begin(),
	              m_cpu_dirty_log.begin() + static_cast<ptrdiff_t>(m_cpu_dirty_count));
	m_cpu_dirty_count    = 0;
	m_cpu_dirty_overflow = false;
	return complete;
}

bool MemoryTracker::CpuDirtyLogIntersects(uint64_t vaddr, uint64_t size) {
	std::scoped_lock lock(m_cpu_dirty_lock);
	if (m_cpu_dirty_overflow) {
		return true;
	}
	for (size_t i = 0; i < m_cpu_dirty_count; i++) {
		const auto& range = m_cpu_dirty_log[i];
		if (range.address < vaddr + size && vaddr < range.End()) {
			return true;
		}
	}
	return false;
}

bool MemoryTracker::IsRegionCpuModified(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	return Iterate<true>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		return manager->IsModified<DirtySource::Cpu>(offset, bytes);
	});
}

bool MemoryTracker::IsRegionCpuModifiedExisting(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	// create=false: a null region is skipped untouched, so this neither creates a region (which
	// would start all-dirty) nor bumps the CPU-dirty generation.
	return Iterate<false>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		return manager->IsModified<DirtySource::Cpu>(offset, bytes);
	});
}

bool MemoryTracker::IsRegionGpuModified(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	return Iterate<false>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		return manager->IsModified<DirtySource::Gpu>(offset, bytes);
	});
}

void MemoryTracker::MarkRegionAsCpuModified(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	Iterate<true>(vaddr, size, [this](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		manager->ChangeState<DirtySource::Cpu, true>(manager->GetCpuAddr() + offset, bytes);
		// Log while the region lock is held: a page seen CPU-dirty is always logged or taken.
		RecordCpuDirty(manager->GetCpuAddr() + offset, bytes);
	});
}

void MemoryTracker::MarkRegionAsGpuModified(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	Iterate<true>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		manager->ChangeState<DirtySource::Gpu, true>(manager->GetCpuAddr() + offset, bytes);
	});
}

void MemoryTracker::UnmarkRegionAsGpuModified(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	Iterate<false>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		std::scoped_lock lock(manager->lock);
		manager->ChangeState<DirtySource::Gpu, false>(manager->GetCpuAddr() + offset, bytes);
	});
}

void MemoryTracker::UntrackMemory(uint64_t vaddr, uint64_t size) {
	CheckNotInUploadCallback();
	std::vector<RegionManager*> managers;
	managers.reserve((vaddr % TRACKER_REGION_SIZE + size + TRACKER_REGION_SIZE - 1) /
	                 TRACKER_REGION_SIZE);
	Iterate<false>(vaddr, size, [&](RegionManager* manager, uint64_t, uint64_t) {
		managers.push_back(manager);
	});

	std::vector<std::unique_lock<TrackingSpinLock>> locks;
	locks.reserve(managers.size());
	for (auto* manager: managers) {
		locks.emplace_back(manager->lock);
	}
	if (Iterate<false>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		    return manager->IsModified<DirtySource::Gpu>(offset, bytes);
	    })) {
		EXIT("cannot untrack GPU-dirty memory\n");
	}
	Iterate<false>(vaddr, size, [](RegionManager* manager, uint64_t offset, uint64_t bytes) {
		manager->ChangeState<DirtySource::Cpu, true>(manager->GetCpuAddr() + offset, bytes);
	});
	RecordCpuDirty(vaddr, size);
}

} // namespace Libs::Graphics
