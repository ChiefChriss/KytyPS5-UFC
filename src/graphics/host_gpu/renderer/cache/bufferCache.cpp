#include "graphics/host_gpu/renderer/cache/bufferCache.h"

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/textureCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/renderer/commandRecorder.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/vramBudget.h"
#include "graphics/host_gpu/vulkanCommon.h"
#include "kernel/memory.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace Libs::Graphics {

namespace {

constexpr uint64_t MiB                    = 1024 * 1024;
constexpr uint64_t GdsBufferSize          = 64 * 1024;
constexpr size_t   BdaRegisteredRangesMax = 256;
constexpr uint32_t ArgsWatchCount         = 64;
constexpr uint64_t ArgsSlotSize           = 64;

// Switches for the Dev 045-053 buffer-cache changes. User bisect (2026-10-05): fighter-mesh
// explosions occur exactly when the BDA dirty-log fast path is on (Dev 073, env tests 2/3), so it
// is opt-in (KYTY_UFC_BDA_DIRTY_LOG=1) until the missed-upload case is found. The frame-based GC
// age stays on (KYTY_UFC_GC_FRAMES=0 restores the fixed age). Indirect-argument host copies are
// opt-in (KYTY_UFC_ARGS_COPY=1).
bool UfcSwitch(const char* name) {
	const char* value = std::getenv(name);
	return value == nullptr || std::strcmp(value, "0") != 0;
}
bool UfcOptIn(const char* name) {
	const char* value = std::getenv(name);
	return value != nullptr && std::strcmp(value, "1") == 0;
}
bool BdaDirtyLogEnabled() {
	static const bool enabled = UfcSwitch("KYTY_UFC_BDA_DIRTY_LOG"); // Dev 094 all-on bisect: on by default, =0 turns it off
	return enabled;
}
bool GcFramesEnabled() {
	static const bool enabled = UfcSwitch("KYTY_UFC_GC_FRAMES");
	return enabled;
}
bool ArgsCopyEnabled() {
	static const bool enabled = UfcSwitch("KYTY_UFC_ARGS_COPY"); // Dev 094 all-on bisect: on by default, =0 turns it off
	return enabled;
}
// KYTY_UFC_BDA_SYNC_MERGE=1 (opt-in): the dirty-log fast path processes up to 256 dirty ranges
// plus registered ranges every BDA draw, one m_buffers tree search per range. Merge them into one
// address-sorted, coalesced list and walk m_buffers once with a retained cursor, as the full-walk
// path already does.
bool BdaSyncMergeEnabled() {
	static const bool enabled = UfcOptIn("KYTY_UFC_BDA_SYNC_MERGE");
	return enabled;
}
// KYTY_UFC_BDA_SYNC_GENERATION=1 (opt-in): skip the BDA walk on calls where no page has
// transitioned to CPU-dirty since the last walked call, no buffer was registered and the mapped
// ranges are unchanged. Unlike the dirty-log fast path, a non-quiescent call always performs the
// full conservative walk, so a lost dirty-log entry can never cause a stale upload.
bool BdaSyncGenerationEnabled() {
	static const bool enabled = UfcOptIn("KYTY_UFC_BDA_SYNC_GENERATION");
	return enabled;
}
// KYTY_UFC_BDA_SYNC_VERIFY=1 (diagnostic, opt-in): before trusting a generation-gated quiescent
// skip, walk the mapped ranges and confirm no tracked buffer still has CPU-dirty pages. A full
// walk uploads exactly the CPU-modified pages, so this oracle is complete; any hit is a real
// missed upload and forces the conservative walk. Costs the full walk, so never default.
bool BdaSyncVerifyEnabled() {
	static const bool enabled = UfcOptIn("KYTY_UFC_BDA_SYNC_VERIFY");
	return enabled;
}
// Default on since Dev 085: BDA reads of GC-retired buffers returned zero (fighter explosions).
// KYTY_UFC_BDA_KEEP=0 restores the old retirement.
bool BdaKeepEnabled() {
	static const bool enabled = UfcSwitch("KYTY_UFC_BDA_KEEP");
	return enabled;
}
constexpr uint32_t BdaVerifyReportLimit = 32;

} // namespace

void BufferCache::WriteDataBuffer(Buffer& buffer, uint64_t address, const void* source,
                                  uint64_t size) {
	auto* bytes = static_cast<const uint8_t*>(source);
	while (size != 0) {
		const auto chunk  = std::min(size, m_staging_buffer.Size());
		const auto offset = m_staging_buffer.Copy(bytes, chunk, 4);
		buffer.CopyFrom(m_scheduler.Current(), m_staging_buffer, offset, buffer.Offset(address),
		                chunk, vk::AccessFlagBits::eHostWrite);
		bytes += chunk;
		address += chunk;
		size -= chunk;
	}
}

void BufferCache::Register(BufferId id) {
	ChangeRegister<true>(id);
}

void BufferCache::Unregister(BufferId id) {
	ChangeRegister<false>(id);
}

void BufferCache::EnsureBdaPageTableInitialized() {
	if (m_bda_pagetable_initialized) {
		return;
	}
	// Opt-in (KYTY_UFC_BDA_TABLE_INIT=1): Dev 087 had it on by default and UFC fighters turned
	// into black silhouettes in 2 of 5 fights (none before), so it stays off until understood.
	static const bool enabled = UfcSwitch("KYTY_UFC_BDA_TABLE_INIT"); // Dev 094 all-on bisect: on by default, =0 turns it off
	if (!enabled) {
		m_bda_pagetable_initialized = true;
		return;
	}
	// Recorded before the first table write or read, and ordered by Fill barriers.
	m_bda_pagetable_buffer.Fill(0, m_bda_pagetable_buffer.Size(), 0);
	m_bda_pagetable_initialized = true;
}

Buffer* BufferCache::GetBdaPageTableBuffer() {
	EnsureBdaPageTableInitialized();
	return &m_bda_pagetable_buffer;
}

template <bool insert>
void BufferCache::ChangeRegister(BufferId id) {
	EnsureBdaPageTableInitialized();
	auto&                buffer = m_slot_buffers[id];
	PageTable::PageRange pages {};
	EXIT_IF(!(GuestRange {buffer.CpuAddress(), buffer.Size()}.Valid()) ||
	        !PageTable::TryGetPageRange(buffer.CpuAddress(), buffer.Size(), pages));
	for (size_t page = pages.first; page < pages.last_exclusive; ++page) {
		if constexpr (insert) {
			m_page_table[page] = id;
		} else {
			m_page_table[page] = {};
		}
	}
	const auto size_pages   = pages.last_exclusive - pages.first;
	const auto table_offset = PageIndex(buffer.CpuAddress()) * sizeof(vk::DeviceAddress);
	if constexpr (insert) {
		const auto [it, inserted] = m_buffers.emplace(buffer.CpuAddress(), id);
		(void)it;
		EXIT_IF(!inserted);
		if (m_bda_registered_ranges.size() >= BdaRegisteredRangesMax) {
			m_bda_registered_ranges.clear();
			m_bda_walk_required = true;
		} else if (!m_bda_walk_required) {
			m_bda_registered_ranges.push_back({buffer.CpuAddress(), buffer.Size()});
		}
		m_total_used_memory += buffer.Size();
		buffer.lru_id = m_lru_cache.Insert(id, m_gc_tick);
		std::vector<vk::DeviceAddress> addresses;
		addresses.reserve(size_pages);
		for (uint64_t i = 0; i < size_pages; ++i) {
			addresses.push_back(buffer.BufferDeviceAddress() + (i << CACHING_PAGEBITS));
		}
		WriteDataBuffer(m_bda_pagetable_buffer, table_offset, addresses.data(),
		                addresses.size() * sizeof(vk::DeviceAddress));
	} else {
		const auto found = m_buffers.find(buffer.CpuAddress());
		EXIT_IF(found == m_buffers.end() || found->second != id);
		m_buffers.erase(found);
		EXIT_IF(buffer.Size() > m_total_used_memory);
		m_total_used_memory -= buffer.Size();
		m_lru_cache.Free(buffer.lru_id);
		m_bda_pagetable_buffer.Fill(table_offset, size_pages * sizeof(vk::DeviceAddress), 0);
		buffer.is_deleted = true;
	}
}

void BufferCache::TouchBuffer(const Buffer& buffer) {
	if (!buffer.is_deleted) {
		m_lru_cache.Touch(buffer.lru_id, m_gc_tick);
	}
}

void BufferCache::DeleteBuffer(BufferId id) {
	if (IsBufferInvalid(id)) {
		return;
	}
	Unregister(id);
	if (m_scheduler.Active()) {
		m_scheduler.DeferOperation([this, id] { m_slot_buffers.erase(id); });
	} else {
		m_slot_buffers.erase(id);
	}
}

template <bool async>
bool BufferCache::DownloadBufferMemory(Buffer& buffer, uint64_t vaddr, uint64_t size) {
	std::vector<vk::BufferCopy> copies;
	uint64_t                    total_size     = 0;
	const auto                  buffer_address = buffer.CpuAddress();
	m_memory_tracker.ForEachDownloadRange<false>(
	    vaddr, size, [&](uint64_t address, uint64_t bytes) noexcept {
		    m_memory_tracker.ValidateGpuDirtyPages(m_gpu_modified_ranges, address, bytes,
		                                           "buffer download");
		    m_gpu_modified_ranges.ForEachInRange(address, bytes, [&](uint64_t start, uint64_t end) {
			    copies.emplace_back(start - buffer_address, total_size, end - start);
			    // Keep packed ranges on separate cache lines, as in shadPS4.
			    total_size += Common::AlignUp(end - start, 64);
		    });
		    m_gpu_modified_ranges.Subtract(address, bytes);
	    });
	if (copies.empty()) {
		return false;
	}

	auto [mapped, offset] = m_download_buffer.Map(total_size, 64);
	std::unique_ptr<Buffer> temporary;
	if (mapped == nullptr) {
		KYTY_PROFILER_BLOCK("BufferCache::TemporaryDownloadBuffer");
		temporary = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Download, 0,
		                                     vk::BufferUsageFlagBits::eTransferDst, total_size);
		mapped    = temporary->Mapped().data();
	} else {
		m_download_buffer.Commit();
	}
	const auto& download = temporary ? *temporary : m_download_buffer;
	for (auto& copy: copies) {
		copy.dstOffset += offset;
	}

	auto& command = m_scheduler.Current();
	command.EndRendering();
	const auto              native = command.Recorder();
	vk::BufferMemoryBarrier before {};
	before.srcAccessMask       = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
	before.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
	before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	before.buffer              = buffer.Handle();
	before.offset              = 0;
	before.size                = buffer.Size();
	native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                       vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 1, &before, 0,
	                       nullptr);
	native.copyBuffer(buffer.Handle(), download.Handle(), static_cast<uint32_t>(copies.size()),
	                  copies.data());

	auto after          = before;
	after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	after.dstAccessMask = vk::AccessFlagBits::eHostRead;
	after.buffer        = download.Handle();
	after.offset        = offset;
	after.size          = total_size;
	native.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                       vk::PipelineStageFlagBits::eAllCommands |
	                           vk::PipelineStageFlagBits::eHost,
	                       {}, 0, nullptr, 1, &after, 0, nullptr);
	uint64_t token = 0;
	if constexpr (async) {
		PendingReadback pending;
		pending.tick = m_scheduler.CurrentTick();
		for (const auto& copy: copies)
			pending.ranges.push_back({buffer_address + copy.srcOffset, copy.size});
		std::lock_guard lock(m_readback_mutex);
		token = ++m_next_readback;
		EXIT_IF(token == 0);
		m_pending_readbacks.emplace(token, std::move(pending));
	}
	auto publish = [this, mapped, offset, total_size, buffer_address, token,
	                copies = std::move(copies), owner = std::move(temporary)] {
		(owner ? *owner : m_download_buffer).Invalidate(offset, total_size);
		for (const auto& copy: copies) {
			Libs::LibKernel::Memory::WriteBacking(buffer_address + copy.srcOffset,
			                                      mapped + (copy.dstOffset - offset), copy.size);
		}
		if (token != 0) {
			std::lock_guard lock(m_readback_mutex);
			for (const auto& copy: copies)
				m_published_readbacks.Add(buffer_address + copy.srcOffset, copy.size);
			EXIT_IF(m_pending_readbacks.erase(token) != 1);
		}
	};
	if constexpr (async) {
		m_scheduler.DeferPriorityOperation(std::move(publish));
	} else {
		const auto tick = m_scheduler.CurrentTick();
		m_scheduler.Wait(tick);
		m_scheduler.WaitPriorityOperations(tick);
		publish();
	}
	return true;
}

BufferCache::BufferCache(GraphicContext& graphics, CommandScheduler& scheduler,
                         PageManager& page_manager, TextureCache& texture_cache)
    : m_graphics(graphics), m_scheduler(scheduler), m_fault_manager(graphics, scheduler, *this),
      m_gds_buffer(graphics, scheduler, MemoryUsage::Stream, 0, AllFlags, GdsBufferSize),
      m_bda_pagetable_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 0, AllFlags,
                             BDA_PAGETABLE_SIZE),
      m_memory_tracker(page_manager),
      m_staging_buffer(graphics, scheduler, MemoryUsage::Upload, 512 * MiB),
      m_stream_buffer(graphics, scheduler, MemoryUsage::Stream, 64 * MiB),
      m_download_buffer(graphics, scheduler, MemoryUsage::Download, 64 * MiB),
      m_device_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 128 * MiB),
      m_texture_cache(texture_cache),
      m_args_download(graphics, scheduler, MemoryUsage::Download, 0, AllFlags,
                      ArgsWatchCount * ArgsSlotSize) {
	m_bda_dirty_verify = UfcOptIn("KYTY_UFC_BDA_DIRTY_VERIFY");
	if (BdaDirtyLogEnabled()) {
		Log::WriteToConsoleAndLog(fmt::format("BDA dirty log: ON{}\n",
		                                      m_bda_dirty_verify ? " (verify)" : ""));
	}
	std::memset(m_gds_buffer.Mapped().data(), 0, static_cast<size_t>(m_gds_buffer.Size()));
	m_gds_buffer.Flush(0, m_gds_buffer.Size());
	SetVulkanObjectNameF(m_graphics.device, m_bda_pagetable_buffer.Handle(),
	                     "BDA Page Table Buffer");
	const auto null_id =
	    m_slot_buffers.insert(m_graphics, m_scheduler, MemoryUsage::DeviceLocal, 0, AllFlags, 16);
	EXIT_IF(null_id != NULL_BUFFER_ID);
	SetVulkanObjectNameF(m_graphics.device, GetBuffer(null_id).Handle(), "Kyty.NullBuffer");
	if (!m_graphics.CanReportMemoryUsage()) {
		return;
	}
	constexpr int64_t GiB              = 1024ll * 1024 * 1024;
	constexpr int64_t target_threshold = 8 * GiB;
	const auto        budget =
	    static_cast<int64_t>(std::min<uint64_t>(m_graphics.GetTotalMemoryBudget(), INT64_MAX));
	const auto threshold = std::min(budget, target_threshold);
	const auto expected  = std::min(budget - 6 * threshold / 10, budget - GiB);
	const auto critical  = std::min(budget - 2 * threshold / 10, budget - GiB / 2);
	m_trigger_gc_memory  = static_cast<uint64_t>(std::max<int64_t>(expected, GiB));
	m_critical_gc_memory = static_cast<uint64_t>(std::max<int64_t>(critical, 2 * GiB));
}

BufferCache::~BufferCache() {
	{
		std::lock_guard lock(m_readback_mutex);
		EXIT_IF(!m_pending_readbacks.empty());
	}
	if (!m_gpu_modified_ranges.Empty()) {
		EXIT("BufferCache: destroyed with pending GPU-modified ranges\n");
	}
	for (const auto& [vaddr, id]: m_buffers) {
		(void)vaddr;
		const auto& buffer = m_slot_buffers[id];
		if (m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size())) {
			EXIT("BufferCache: destroyed with GPU-modified buffer\n");
		}
	}
	m_buffers.clear();
}

void BufferCache::InvalidateMemory(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid memory-invalidation range\n");
	}
	m_memory_tracker.InvalidateRegion(vaddr, size,
	                                  [this, vaddr, size] { ReadMemory(vaddr, size, true); });
}

void BufferCache::ReadMemory(uint64_t vaddr, uint64_t size, bool is_write, WaitSite site) {
	if (site == WaitSite::Count) {
		site = is_write ? WaitSite::CpuWriteFlush : WaitSite::CpuReadFault;
	}
	if (!GuestGpu::IsGpuThread() && CommandScheduler::InDeferredOperation()) {
		EXIT("unsupported buffer readback from an asynchronous GPU completion, "
		     "addr=0x%016" PRIx64 " size=0x%016" PRIx64 "\n",
		     vaddr, size);
	}
	static const bool async_enabled = [] {
		const char* value = std::getenv("KYTY_ASYNC_READBACK");
		return value == nullptr || std::strcmp(value, "0") != 0;
	}();
	const bool async = async_enabled && !GuestGpu::IsGpuThread();
	for (;;) {
		uint64_t tick = 0;
		m_scheduler.Context().GetGpu().SendCommandSync([&] {
			const ScopedWaitSite wait_site(site);
			tick = ReadMemoryStep(vaddr, size, is_write, async);
		});
		if (tick == 0) return;
		const ScopedWaitSite wait_site(site);
		m_readback_waiters.fetch_add(1, std::memory_order_release);
		m_scheduler.WaitPriorityOperations(tick);
		m_readback_waiters.fetch_sub(1, std::memory_order_release);
	}
}

uint64_t BufferCache::PendingReadbackTick(uint64_t vaddr, uint64_t size) const {
	EXIT_IF(!(GuestRange{vaddr, size}.Valid()));
	std::lock_guard lock(m_readback_mutex);
	uint64_t latest = 0;
	for (const auto& [token, pending]: m_pending_readbacks) {
		(void)token;
		for (const auto& range: pending.ranges) {
			if (vaddr < range.End() && range.address < vaddr + size) {
				latest = std::max(latest, pending.tick);
				break;
			}
		}
	}
	return latest;
}

uint64_t BufferCache::ReadMemoryStep(uint64_t vaddr, uint64_t size, bool is_write, bool async) {
	if (is_write && !IsRegionRegistered(vaddr, size)) return 0;
	KYTY_PROFILER_BLOCK("BufferCache::ReadMemoryStep", profiler::colors::RedA100);
	auto& buffer = m_slot_buffers[FindBuffer(vaddr, size)];
	// Senaxx's 4 MiB grouping is tunable; 0 means only requested tracker pages.
	static const uint64_t window = [] {
		const char* value = std::getenv("KYTY_READBACK_WINDOW_KB");
		const uint64_t kb = value ? std::strtoull(value, nullptr, 10) : 4096;
		if (kb == 0) return uint64_t{0};
		if (kb < 4 || kb > 65536 || (kb & (kb - 1)) != 0) {
			Log::WriteToConsoleAndLog("Readback window: invalid size; using 4096 KiB\n");
			return uint64_t{4096 * 1024};
		}
		return kb * 1024;
	}();
	const auto alignment = window ? window : TRACKER_PAGE_SIZE;
	const auto begin = std::max(Common::AlignDown(vaddr, alignment), buffer.CpuAddress());
	const auto end = std::min(std::max(begin + alignment, Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE)),
	                          buffer.CpuAddress() + buffer.Size());
	if (const auto pending = PendingReadbackTick(begin, end - begin); pending != 0) {
		if (async) {
			if (pending == m_scheduler.CurrentTick()) m_scheduler.Flush();
			return pending;
		}
		m_scheduler.Wait(pending);
		m_scheduler.WaitPriorityOperations(pending);
	}
	ResolvePublishedReadbackPages(begin, end - begin);
	if (async) {
		if (DownloadBufferMemory<true>(buffer, begin, end - begin)) {
			const auto tick = PendingReadbackTick(begin, end - begin);
			EXIT_IF(tick == 0);
			if (tick == m_scheduler.CurrentTick()) m_scheduler.Flush();
			return tick;
		}
	} else {
		(void)DownloadBufferMemory<false>(buffer, begin, end - begin);
	}
	// No dirty bytes and no unfinished publication: lifting protection is now safe,
	// including false-sharing pages whose earlier download has already completed.
	EXIT_IF(m_gpu_modified_ranges.Intersects(begin, end - begin) || PendingReadbackTick(begin, end - begin) != 0);
	m_memory_tracker.UnmarkRegionAsGpuModified(begin, end - begin);
	if (is_write) m_memory_tracker.MarkRegionAsCpuModified(vaddr, size);
	return 0;
}

void BufferCache::ResolvePublishedReadbackPages(uint64_t vaddr, uint64_t size) {
	// Only the GPU thread retires page ownership. Completion callbacks publish bytes
	// and a marker; they never race a tracker transition or overwrite another token.
	if (PendingReadbackTick(vaddr, size) != 0) return;
	std::lock_guard lock(m_readback_mutex);
	const auto begin = Common::AlignDown(vaddr, TRACKER_PAGE_SIZE);
	const auto end = Common::AlignUp(vaddr + size, TRACKER_PAGE_SIZE);
	for (auto page = begin; page < end; page += TRACKER_PAGE_SIZE) {
		if (m_published_readbacks.Intersects(page, TRACKER_PAGE_SIZE) &&
		    !m_gpu_modified_ranges.Intersects(page, TRACKER_PAGE_SIZE)) {
			m_memory_tracker.UnmarkRegionAsGpuModified(page, TRACKER_PAGE_SIZE);
			m_published_readbacks.Subtract(page, TRACKER_PAGE_SIZE);
		}
	}
}

BufferId BufferCache::FindBuffer(uint64_t vaddr, uint64_t size) {
	if (vaddr == 0) {
		return NULL_BUFFER_ID;
	}
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid buffer discovery request\n");
	}
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner != nullptr && *owner) {
		auto& buffer = m_slot_buffers[*owner];
		if (buffer.IsInBounds(vaddr, size)) {
			return *owner;
		}
	}
	return CreateBuffer(vaddr, size);
}

BufferCache::OverlapResult BufferCache::ResolveOverlaps(uint64_t vaddr, uint64_t size) {
	static constexpr int      StreamLeapThreshold = 16;
	static constexpr uint64_t StreamLeapSize      = CACHING_PAGESIZE * 128;

	auto       begin      = vaddr;
	auto       end        = vaddr + size;
	const auto find_first = [&](uint64_t address) {
		auto first = m_buffers.lower_bound(address);
		if (first != m_buffers.begin()) {
			const auto  previous = std::prev(first);
			const auto& buffer   = m_slot_buffers[previous->second];
			if (buffer.CpuAddress() + buffer.Size() > address) {
				first = previous;
			}
		}
		return first;
	};
	auto first           = find_first(begin);
	auto last            = first;
	int  stream_score    = 0;
	bool has_stream_leap = false;
	for (; last != m_buffers.end() && last->first < end; ++last) {
		const auto& buffer        = m_slot_buffers[last->second];
		const auto  buffer_begin  = buffer.CpuAddress();
		const auto  buffer_end    = buffer_begin + buffer.Size();
		const bool  expands_left  = buffer_begin < begin;
		const bool  expands_right = buffer_end > end;
		begin                     = std::min(begin, buffer_begin);
		end                       = std::max(end, buffer_end);
		if (!has_stream_leap && (stream_score += buffer.StreamScore()) > StreamLeapThreshold) {
			has_stream_leap = true;
			// Reserve space in the incoming stream's direction of growth.
			// The old buffer extending left of the request predicts growth to the right, and vice
			// versa.
			if (expands_left) {
				end += std::min(StreamLeapSize, (vaddr < LOWER_ADDRESS_SIZE
				                                     ? LOWER_ADDRESS_SIZE
				                                     : LibKernel::Memory::kExtendedMemoryBase +
				                                           LibKernel::Memory::kExtendedMemorySize) -
				                                    end);
			}
			if (expands_right) {
				const auto minimum = vaddr < LOWER_ADDRESS_SIZE
				                         ? CACHING_PAGESIZE * 2
				                         : LibKernel::Memory::kExtendedMemoryBase;
				if (begin > minimum) {
					begin -= std::min(StreamLeapSize, begin - minimum);
				}
				first = find_first(begin);
				begin = std::min(begin, first->first);
			}
		}
	}
	return {first, last, begin, end, has_stream_leap};
}

void BufferCache::JoinOverlap(BufferId new_id, BufferId overlap_id, bool accumulate_stream_score) {
	auto& new_buffer = m_slot_buffers[new_id];
	auto& overlap    = m_slot_buffers[overlap_id];
	if (accumulate_stream_score) {
		new_buffer.IncreaseStreamScore(overlap.StreamScore() + 1);
	}
	new_buffer.CopyFrom(m_scheduler.Current(), overlap, 0,
	                    overlap.CpuAddress() - new_buffer.CpuAddress(), overlap.Size());
	DeleteBuffer(overlap_id);
}

BufferId BufferCache::CreateBuffer(uint64_t vaddr, uint64_t size) {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(m_scheduler.Current().IsInvalid());
	const auto end     = Common::AlignUp(vaddr + size, CACHING_PAGESIZE);
	vaddr              = Common::AlignDown(vaddr, CACHING_PAGESIZE);
	size               = end - vaddr;
	const auto overlap = ResolveOverlaps(vaddr, size);

	const auto id = m_slot_buffers.insert(
	    m_graphics, m_scheduler, MemoryUsage::DeviceLocal, overlap.begin,
	    AllFlags | vk::BufferUsageFlagBits::eShaderDeviceAddress, overlap.end - overlap.begin);
	const auto& buffer = m_slot_buffers[id];
	SetVulkanObjectNameF(m_graphics.device, buffer.Handle(),
	                     "Kyty.GameBuffer[guest=0x{:016x} size=0x{:x}]", overlap.begin,
	                     overlap.end - overlap.begin);
	for (auto it = overlap.first; it != overlap.last;) {
		const auto old_id = (it++)->second;
		JoinOverlap(id, old_id, !overlap.has_stream_leap);
	}
	Register(id);
	WaitStats::NoteBufferCreated();
	return id;
}

bool BufferCache::SynchronizeBuffer(Buffer& buffer, uint64_t vaddr, uint64_t size, bool is_written,
                                    bool is_texel_buffer) {
	std::vector<vk::BufferCopy> copies;
	uint64_t                    total_size = 0;
	vk::Buffer                  source;
	m_memory_tracker.ForEachUploadRange(
	    vaddr, size, is_written,
	    [&](uint64_t address, uint64_t bytes) noexcept {
		    copies.emplace_back(total_size, buffer.Offset(address), bytes);
		    total_size += bytes;
	    },
	    [&]() noexcept { source = UploadCopies(buffer, copies, total_size); });
	if (source) {
		auto& command = m_scheduler.Current();
		command.EndRendering();
		const auto              native = command.Recorder();
		vk::BufferMemoryBarrier before {};
		before.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite |
		                       vk::AccessFlagBits::eTransferRead |
		                       vk::AccessFlagBits::eTransferWrite;
		before.dstAccessMask       = vk::AccessFlagBits::eTransferWrite;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer              = buffer.Handle();
		before.offset              = 0;
		before.size                = buffer.Size();
		native.pipelineBarrier(
		    vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
		    vk::DependencyFlagBits::eByRegion, 0, nullptr, 1, &before, 0, nullptr);
		native.copyBuffer(source, buffer.Handle(), static_cast<uint32_t>(copies.size()),
		                  copies.data());
		auto after          = before;
		after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		after.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		native.pipelineBarrier(
		    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
		    vk::DependencyFlagBits::eByRegion, 0, nullptr, 1, &after, 0, nullptr);
	}
	if (is_texel_buffer && !is_written) {
		return SynchronizeBufferFromImage(buffer, vaddr, size);
	}
	return false;
}

vk::Buffer BufferCache::UploadCopies(Buffer& buffer, std::span<vk::BufferCopy> copies,
                                     uint64_t total_size) {
	if (copies.empty()) {
		return nullptr;
	}

	auto [mapped, base_offset] = m_staging_buffer.Map(total_size, 4);
	if (mapped != nullptr) {
		for (auto& copy: copies) {
			const auto address = buffer.CpuAddress() + copy.dstOffset;
			std::memcpy(mapped + copy.srcOffset, reinterpret_cast<const void*>(address), copy.size);
			copy.srcOffset += base_offset;
		}
		m_staging_buffer.Commit();
		return m_staging_buffer.Handle();
	}

	auto temporary = std::make_unique<Buffer>(m_graphics, m_scheduler, MemoryUsage::Upload, 0,
	                                          vk::BufferUsageFlagBits::eTransferSrc, total_size);
	for (const auto& copy: copies) {
		const auto address = buffer.CpuAddress() + copy.dstOffset;
		std::memcpy(temporary->Mapped().data() + copy.srcOffset,
		            reinterpret_cast<const void*>(address), copy.size);
	}
	temporary->Flush(0, total_size);
	const auto handle = temporary->Handle();
	m_scheduler.DeferOperation([owner = std::move(temporary)]() mutable { owner.reset(); });
	return handle;
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBuffer(uint64_t vaddr, uint64_t size,
                                                       bool is_written, bool is_texel_buffer,
                                                       BufferId id) {
	auto& command = m_scheduler.Current();
	if (command.IsInvalid() || !GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: buffer request requires a recording command buffer\n");
	}

	if (!is_written && size <= CACHING_PAGESIZE &&
	    !m_memory_tracker.IsRegionGpuModified(vaddr, size) &&
	    m_memory_tracker.IsRegionCpuModified(vaddr, size)) {
		const auto alignment = std::max<uint64_t>(
		    m_graphics.physical_device_properties.limits.minUniformBufferOffsetAlignment, 1);
		auto [mapped, offset] = m_stream_buffer.Map(size, alignment, false);
		if (mapped != nullptr) {
			std::memcpy(mapped, reinterpret_cast<const void*>(vaddr), size);
			m_stream_buffer.Commit();
			return {&m_stream_buffer, offset};
		}
	}

	if (IsBufferInvalid(id) || !m_slot_buffers[id].IsInBounds(vaddr, size)) {
		id = FindBuffer(vaddr, size);
	}
	auto& buffer = m_slot_buffers[id];
	TouchBuffer(buffer);
	(void)SynchronizeBuffer(buffer, vaddr, size, is_written, is_texel_buffer);
	if (is_written) {
		m_gpu_modified_ranges.Add(vaddr, size);
		if (!m_args_watches.empty()) {
			for (auto& watch: m_args_watches) {
				if (vaddr < watch.address + watch.size && watch.address < vaddr + size) {
					watch.pending  = true;
					watch.valid    = false;
					m_args_pending = true;
				}
			}
		}
		auto source = m_gpu_write_source;
		if (m_uniform_fill.address == vaddr && m_uniform_fill.size == size) {
			source         = 3;
			m_uniform_fill = {};
		}
		for (auto& watch: m_watched_writes) {
			if (vaddr < watch.address + watch.size && watch.address < vaddr + size) {
				if (source == 0 && (watch.writers & 1u) == 0 && m_writer_logs < 48) {
					m_writer_logs++;
					Log::WriteToConsoleAndLog(fmt::format(
					    "GPU metadata writer: shader=0x{:016x} write=0x{:x}+0x{:x} "
					    "metadata=0x{:x}+0x{:x}\n",
					    m_writer_shader, vaddr, size, watch.address, watch.size));
				}
				watch.writers |= 1u << source;
			}
		}
	}
	return {&buffer, buffer.Offset(vaddr)};
}

void BufferCache::RecordPendingArgsCopies() {
	if (!m_args_pending || !ArgsCopyEnabled()) {
		return;
	}
	m_args_pending = false;
	auto& command  = m_scheduler.Current();
	command.EndRendering();
	const auto native = command.Recorder();
	for (auto& watch: m_args_watches) {
		if (!watch.pending) {
			continue;
		}
		auto& buffer = m_slot_buffers[FindBuffer(watch.address, watch.size)];
		vk::BufferMemoryBarrier before {};
		before.srcAccessMask       = vk::AccessFlagBits::eMemoryWrite;
		before.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
		before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		before.buffer              = buffer.Handle();
		before.offset              = buffer.Offset(watch.address);
		before.size                = watch.size;
		native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
		                       vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 1, &before, 0,
		                       nullptr);
		const vk::BufferCopy copy {buffer.Offset(watch.address), watch.slot * ArgsSlotSize,
		                           watch.size};
		native.copyBuffer(buffer.Handle(), m_args_download.Handle(), 1, &copy);
		auto after          = before;
		after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		after.dstAccessMask = vk::AccessFlagBits::eHostRead;
		after.buffer        = m_args_download.Handle();
		after.offset        = watch.slot * ArgsSlotSize;
		native.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                       vk::PipelineStageFlagBits::eHost, {}, 0, nullptr, 1, &after, 0,
		                       nullptr);
		watch.tick      = m_scheduler.CurrentTick();
		watch.bda_epoch = m_bda_epoch;
		watch.pending   = false;
		watch.valid     = true;
	}
}

void BufferCache::WatchIndirectArgs(uint64_t vaddr, uint64_t size) {
	if (size == 0 || size > ArgsSlotSize || !GuestRange {vaddr, size}.Valid()) {
		return;
	}
	const bool watched =
	    std::any_of(m_args_watches.begin(), m_args_watches.end(),
	                [&](const auto& w) { return w.address == vaddr && w.size == size; });
	if (!watched && m_args_watches.size() < ArgsWatchCount) {
		m_args_watches.push_back(
		    {vaddr, size, 0, 0, static_cast<uint32_t>(m_args_watches.size()), false, false});
	}
}

bool BufferCache::TryReadIndirectArgs(uint64_t vaddr, uint64_t size, void* out) {
	if (!ArgsCopyEnabled() || size == 0 || size > ArgsSlotSize || !GuestRange {vaddr, size}.Valid()) {
		return false;
	}
	auto watch = std::find_if(m_args_watches.begin(), m_args_watches.end(), [&](const auto& w) {
		return w.address == vaddr && w.size == size;
	});
	if (watch == m_args_watches.end()) {
		if (m_args_watches.size() < ArgsWatchCount) {
			m_args_watches.push_back(
			    {vaddr, size, 0, 0, static_cast<uint32_t>(m_args_watches.size()), false, false});
		}
		WaitStats::NoteIndirectArgs(4);
		return false;
	}
	if (!IsRegionGpuModified(vaddr, size)) {
		WaitStats::NoteIndirectArgs(1);
		return false;
	}
	if (!watch->valid || watch->pending || watch->bda_epoch != m_bda_epoch) {
		WaitStats::NoteIndirectArgs(2);
		return false;
	}
	if (watch->tick >= m_scheduler.CurrentTick()) {
		WaitStats::NoteIndirectArgs(3);
		return false;
	}
	{
		const ScopedWaitSite wait_site(WaitSite::IndirectArgsCopy);
		m_scheduler.Wait(watch->tick);
	}
	const auto offset = watch->slot * ArgsSlotSize;
	m_args_download.Invalidate(offset, ArgsSlotSize);
	std::memcpy(out, m_args_download.Mapped().data() + offset, size);
	WaitStats::NoteIndirectArgs(0);
	return true;
}

bool BufferCache::TryObtainGpuIndirectArgs(uint64_t vaddr, uint64_t size, Buffer** out_buffer,
                                            uint64_t* out_offset) {
	if (size == 0 || size > ArgsSlotSize || !GuestRange {vaddr, size}.Valid()) {
		return false;
	}
	if (!IsRegionGpuModified(vaddr, size)) {
		// The CPU copy is already current; no drain is avoided by going GPU-sourced.
		return false;
	}
	auto [buffer, offset] = ObtainBuffer(vaddr, size, false);
	if ((offset & 3u) != 0) {
		return false;
	}
	// Order earlier compute/DMA writes in this command buffer before the indirect read.
	// No submit and no wait: the draw stays on the GPU timeline.
	auto& command = m_scheduler.Current();
	command.EndRendering();
	const auto              native = command.Recorder();
	vk::BufferMemoryBarrier barrier {};
	barrier.srcAccessMask       = vk::AccessFlagBits::eMemoryWrite;
	barrier.dstAccessMask       = vk::AccessFlagBits::eIndirectCommandRead;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = buffer->Handle();
	barrier.offset              = offset;
	barrier.size                = size;
	native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                       vk::PipelineStageFlagBits::eDrawIndirect, {}, 0, nullptr, 1, &barrier,
	                       0, nullptr);
	if (out_buffer != nullptr) {
		*out_buffer = buffer;
	}
	if (out_offset != nullptr) {
		*out_offset = offset;
	}
	return true;
}

bool BufferCache::TryObtainGpuIndexBinding(uint64_t base_vaddr, uint64_t element_size,
                                            Buffer** out_buffer, uint64_t* out_offset,
                                            uint64_t* out_span_size) {
	if (base_vaddr == 0 || (element_size != 2 && element_size != 4)) {
		return false;
	}
	if (!GuestRange {base_vaddr, element_size}.Valid()) {
		return false;
	}
	// Only reuse the already-cached allocation: creating/joining a buffer for an unknown
	// index count here would churn the cache on every draw. The CPU path (exact count)
	// remains responsible for growing allocations.
	const auto* owner = m_page_table.Find(base_vaddr >> PageTable::kPageBits);
	if (owner == nullptr || !*owner || IsBufferInvalid(*owner)) {
		return false;
	}
	auto& cached = m_slot_buffers[*owner];
	if (!cached.IsInBounds(base_vaddr, element_size)) {
		return false;
	}
	const uint64_t span = cached.CpuAddress() + cached.Size() - base_vaddr;
	// A huge remainder would make the superset upload walk expensive; the CPU path with
	// its exact count stays responsible for those draws.
	static constexpr uint64_t kMaxIndexSpan = 32ull * 1024 * 1024;
	if (span < element_size || span > kMaxIndexSpan) {
		return false;
	}
	// Touch and synchronize CPU-dirty pages over the remainder so any GPU-provided
	// firstIndex/count within the allocation reads current data. Static index data is
	// clean, so this is normally just a tracker walk, not an upload.
	auto [buffer, offset] = ObtainBuffer(base_vaddr, span, false);
	// Order earlier GPU writes in this command buffer before the index read. No submit
	// and no wait: the draw stays on the GPU timeline.
	auto& command = m_scheduler.Current();
	command.EndRendering();
	const auto              native = command.Recorder();
	vk::BufferMemoryBarrier barrier {};
	barrier.srcAccessMask       = vk::AccessFlagBits::eMemoryWrite;
	barrier.dstAccessMask       = vk::AccessFlagBits::eIndexRead;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer              = buffer->Handle();
	barrier.offset              = offset;
	barrier.size                = span;
	native.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                       vk::PipelineStageFlagBits::eVertexInput, {}, 0, nullptr, 1, &barrier,
	                       0, nullptr);
	if (out_buffer != nullptr) {
		*out_buffer = buffer;
	}
	if (out_offset != nullptr) {
		*out_offset = offset;
	}
	if (out_span_size != nullptr) {
		*out_span_size = span;
	}
	return true;
}

uint32_t BufferCache::TakeMetadataWriter(uint64_t vaddr, uint64_t size) {
	for (auto& watch: m_watched_writes) {
		if (watch.address == vaddr && watch.size == size) {
			return std::exchange(watch.writers, 0u);
		}
	}
	if (m_watched_writes.size() < 512) {
		m_watched_writes.push_back({vaddr, size, 0u});
	}
	return 0u;
}

std::pair<Buffer*, uint64_t> BufferCache::ObtainBufferForImage(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid image source\n");
	}
	const auto* owner = m_page_table.Find(vaddr >> PageTable::kPageBits);
	if (owner != nullptr && *owner) {
		auto& buffer = m_slot_buffers[*owner];
		if (buffer.IsInBounds(vaddr, size)) {
			TouchBuffer(buffer);
			(void)SynchronizeBuffer(buffer, vaddr, size, false, false);
			return {&buffer, buffer.Offset(vaddr)};
		}
	}
	if (IsRegionGpuModified(vaddr, size)) {
		return ObtainBuffer(vaddr, size, false, false);
	}

	auto [staging, stage_offset] = m_staging_buffer.Map(size, 16);
	if (staging == nullptr ||
	    !Libs::LibKernel::Memory::TryReadSparseBacking(vaddr, staging, size)) {
		EXIT("BufferCache: failed to read mapped guest image backing\n");
	}
	m_staging_buffer.Commit();
	return {&m_staging_buffer, stage_offset};
}

void BufferCache::FillBuffer(uint64_t vaddr, uint64_t size, uint32_t value, bool is_gds) {
	if ((vaddr & 3u) != 0 || size == 0 || (size & 3u) != 0 || size > UINT64_MAX - vaddr) {
		EXIT("BufferCache: fill range must be dword aligned\n");
	}
	if (is_gds) {
		if (vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - vaddr) {
			EXIT("BufferCache: GDS fill range is out of bounds\n");
		}
		m_gds_buffer.Fill(vaddr, size, value);
		return;
	}
	if (vaddr == 0) {
		EXIT("BufferCache: invalid fill memory address\n");
	}
	(void)m_texture_cache.ClearMeta(vaddr);
	if (!IsRegionGpuModified(vaddr, size)) {
		// Access the guest mapping so write faults invalidate cached buffers and images.
		auto* destination = reinterpret_cast<uint32_t*>(vaddr);
		std::fill(destination, destination + size / sizeof(uint32_t), value);
		return;
	}

	m_texture_cache.InvalidateMemoryFromGPU(vaddr, size);
	m_gpu_write_source      = 1;
	auto [dst, dst_offset] = ObtainBuffer(vaddr, size, true, true);
	m_gpu_write_source      = 0;
	dst->Fill(dst_offset, size, value);
}

void BufferCache::FillBufferCoherent(uint64_t vaddr, uint64_t size, uint32_t value) {
	const bool whole_pages = ((vaddr | size) & (TRACKER_PAGE_SIZE - 1)) == 0;
	const bool gpu_owned   = whole_pages && size != 0 && IsRegionGpuModified(vaddr, size);
	FillBuffer(vaddr, size, value, false);
	if (!gpu_owned) {
		return;
	}
	// The fill was recorded on the GPU timeline and owns every page of the range. Earlier GPU
	// commands still read their own copies; guest memory now matches the post-fill contents.
	const std::vector<uint32_t> words(size / sizeof(uint32_t), value);
	Libs::LibKernel::Memory::WriteBacking(vaddr, words.data(), size);
	m_gpu_modified_ranges.Subtract(vaddr, size);
	m_memory_tracker.UnmarkRegionAsGpuModified(vaddr, size);
}

void BufferCache::CopyBuffer(uint64_t dst_vaddr, uint64_t src_vaddr, uint64_t size, bool dst_gds,
                             bool src_gds) {
	const bool dst_memory = !dst_gds;
	const bool src_memory = !src_gds;
	if ((dst_memory && dst_vaddr == 0) || (src_memory && src_vaddr == 0) || size == 0 ||
	    ((dst_gds || src_gds) && ((dst_vaddr | src_vaddr | size) & 3u) != 0) ||
	    size > UINT64_MAX - dst_vaddr || size > UINT64_MAX - src_vaddr || (dst_gds && src_gds) ||
	    (dst_gds && (dst_vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - dst_vaddr)) ||
	    (src_gds && (src_vaddr > m_gds_buffer.Size() || size > m_gds_buffer.Size() - src_vaddr))) {
		EXIT("BufferCache: invalid copy range, src=0x%016" PRIx64 " dst=0x%016" PRIx64
		     " size=0x%016" PRIx64 " src_gds=%d dst_gds=%d\n",
		     src_vaddr, dst_vaddr, size, static_cast<int>(src_gds), static_cast<int>(dst_gds));
	}
	if (src_memory && dst_memory && !IsRegionGpuModified(dst_vaddr, size) &&
	    !IsRegionGpuModified(src_vaddr, size) &&
	    !m_texture_cache.FindImageFromRange(src_vaddr, size)) {
		std::memcpy(reinterpret_cast<void*>(dst_vaddr), reinterpret_cast<const void*>(src_vaddr),
		            size);
		return;
	}

	auto& command = m_scheduler.Current();
	if (dst_memory) {
		m_texture_cache.InvalidateMemoryFromGPU(dst_vaddr, size);
	}
	const auto src_id      = src_memory ? FindBuffer(src_vaddr, size) : BufferId {};
	const auto dst_id      = dst_memory ? FindBuffer(dst_vaddr, size) : BufferId {};
	auto [src, src_offset] = src_memory ? ObtainBuffer(src_vaddr, size, false, true, src_id)
	                                    : std::pair {&m_gds_buffer, src_vaddr};
	m_gpu_write_source     = 2;
	auto [dst, dst_offset] = dst_memory ? ObtainBuffer(dst_vaddr, size, true, true, dst_id)
	                                    : std::pair {&m_gds_buffer, dst_vaddr};
	m_gpu_write_source     = 0;
	dst->CopyFrom(command, *src, src_offset, dst_offset, size);
}

bool BufferCache::IsRegionRegistered(uint64_t vaddr, uint64_t size) {
	if (!GuestRange {vaddr, size}.Valid()) {
		EXIT("BufferCache: invalid registered-region query\n");
	}
	// Cached buffers are ordered and non-overlapping. The last buffer beginning before the query
	// end is therefore the only possible intersection.
	const auto candidate = m_buffers.lower_bound(vaddr + size);
	if (candidate == m_buffers.begin()) {
		return false;
	}
	const auto& [address, id] = *std::prev(candidate);
	return address + m_slot_buffers[id].Size() > vaddr;
}

bool BufferCache::IsRegionGpuModified(uint64_t vaddr, uint64_t size) {
	return m_memory_tracker.IsRegionGpuModified(vaddr, size);
}

bool BufferCache::HasGpuDirtyBytes(uint64_t vaddr, uint64_t size) {
	return m_gpu_modified_ranges.Intersects(vaddr, size);
}

bool BufferCache::IsRegionCpuModified(uint64_t vaddr, uint64_t size) {
	return m_memory_tracker.IsRegionCpuModified(vaddr, size);
}

void BufferCache::RunGarbageCollector() {
	const auto tick = m_gc_tick++;
	if (m_gc_frame != m_gc_seen_frame) {
		const auto frames = m_gc_frame > m_gc_seen_frame ? m_gc_frame - m_gc_seen_frame : 1;
		if (m_gc_seen_frame != 0) {
			m_gc_ticks_per_frame = (tick - m_gc_frame_tick) / frames;
		}
		m_gc_seen_frame = m_gc_frame;
		m_gc_frame_tick = tick;
	}
	if (m_graphics.CanReportMemoryUsage()) {
		m_total_used_memory = m_graphics.GetDeviceMemoryUsage();
	}
	// KYTY_UFC_VRAM_GC_BUDGET=1 (vramBudget.h): the marks follow the planning budget as it is now
	// (re-read once per guest frame); this collector, blind to BDA reads apart from the keep
	// logic below, runs only above the budget itself and downloads only above B + B/16.
	auto trigger  = m_trigger_gc_memory;
	auto critical = m_critical_gc_memory;
	if (VramBudget::GcEnabled() && m_graphics.CanReportMemoryUsage()) {
		if (m_gc_frame != m_budget_frame) {
			m_budget_frame    = m_gc_frame;
			const auto budget = m_graphics.GetTotalMemoryBudget();
			m_budget_trigger =
			    budget != 0 ? VramBudget::BufferTrigger(budget) : m_trigger_gc_memory;
			m_budget_critical =
			    budget != 0 ? VramBudget::BufferCritical(budget) : m_critical_gc_memory;
		}
		trigger  = m_budget_trigger;
		critical = m_budget_critical;
	}
	if (m_total_used_memory < trigger) {
		return;
	}
	KYTY_PROFILER_BLOCK("BufferCache::RunGarbageCollector");

	const bool aggressive = m_total_used_memory >= critical;
	// GC ticks advance once per command-processor pass, and UFC 5 runs ~100-150 of them per
	// frame, so the fixed 160-tick age retired clean buffers that every frame reuses (~700
	// re-creations and re-uploads per second). Below the critical threshold, retire only
	// buffers unused for two frames; the critical path keeps its short age.
	const uint64_t frame_age =
	    GcFramesEnabled() ? std::min<uint64_t>(2 * m_gc_ticks_per_frame, 4096) : 0;
	const uint64_t age =
	    std::min<uint64_t>(aggressive ? 80 : std::max<uint64_t>(160, frame_age), tick);
	const size_t   limit      = aggressive ? 64 : 32;

	std::vector<BufferId> dirty_buffers;
	std::vector<BufferId> kept_buffers;
	size_t                retire_count = 0;
	m_lru_cache.ForEachItemBelow(tick - age, [&](BufferId id) {
		auto& buffer = m_slot_buffers[id];
		EXIT_IF(buffer.is_deleted);
		if (buffer.bda_resident ||
		    (BdaKeepEnabled() &&
		     m_bda_hot_ranges.Intersects(buffer.CpuAddress(), buffer.Size()))) {
			buffer.bda_resident = true;
			kept_buffers.push_back(id);
			return false;
		}
		if (PendingReadbackTick(buffer.CpuAddress(), buffer.Size()) != 0) return false;
		ResolvePublishedReadbackPages(buffer.CpuAddress(), buffer.Size());
		m_memory_tracker.ValidateGpuDirtyOwnership(m_gpu_modified_ranges, buffer.CpuAddress(),
		                                           buffer.Size(), "garbage collection");
		const bool dirty = m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size());
		if (dirty && !aggressive) {
			return false;
		}
		if (dirty) {
			EXIT_NOT_IMPLEMENTED(!DownloadBufferMemory<true>(buffer, buffer.CpuAddress(), buffer.Size()));
			dirty_buffers.push_back(id);
		} else {
			m_memory_tracker.UntrackMemory(buffer.CpuAddress(), buffer.Size());
			DeleteBuffer(id);
		}
		WaitStats::NoteBufferRetired();
		return ++retire_count == limit;
	});
	// Move kept BDA buffers to the young end so later passes do not rescan them.
	for (const auto id: kept_buffers) {
		TouchBuffer(m_slot_buffers[id]);
	}
	if (!kept_buffers.empty()) {
		WaitStats::NoteBdaFaults(1, kept_buffers.size());
	}
	if (dirty_buffers.empty()) {
		return;
	}

	// Publish all queued downloads before releasing their tracked pages and owners.
	const auto           completion_tick = m_scheduler.CurrentTick();
	const ScopedWaitSite wait_site(WaitSite::BufferGc);
	m_scheduler.Wait(completion_tick);
	m_scheduler.WaitPriorityOperations(completion_tick);
	for (const auto id: dirty_buffers) {
		auto& buffer = m_slot_buffers[id];
		m_memory_tracker.UnmarkRegionAsGpuModified(buffer.CpuAddress(), buffer.Size());
		if (m_memory_tracker.IsRegionGpuModified(buffer.CpuAddress(), buffer.Size()) ||
		    m_gpu_modified_ranges.Intersects(buffer.CpuAddress(), buffer.Size())) {
			EXIT("BufferCache: garbage collection retained GPU ownership\n");
		}
		m_memory_tracker.UntrackMemory(buffer.CpuAddress(), buffer.Size());
		Unregister(id);
		m_slot_buffers.erase(id);
	}
}

void BufferCache::HandleBdaFault(uint64_t vaddr, uint64_t size) {
	const auto id = FindBuffer(vaddr, size);
	if (BdaKeepEnabled()) {
		m_bda_hot_ranges.Add(vaddr, size);
		if (!IsBufferInvalid(id)) {
			m_slot_buffers[id].bda_resident = true;
		}
	}
}

void BufferCache::ProcessFaultBuffer() {
	m_fault_manager.ProcessFaultBuffer();
}

void BufferCache::SynchronizeBuffersInRange(uint64_t vaddr, uint64_t size) {
	const auto end = vaddr + size;
	auto       it  = m_buffers.upper_bound(vaddr);
	if (it != m_buffers.begin()) {
		--it;
	}
	for (; it != m_buffers.end() && it->first < end; ++it) {
		auto&      buffer = m_slot_buffers[it->second];
		const auto start  = std::max(buffer.CpuAddress(), vaddr);
		const auto finish = std::min(buffer.CpuAddress() + buffer.Size(), end);
		if (start < finish) {
			(void)SynchronizeBuffer(buffer, start, finish - start, false, false);
		}
	}
}

void BufferCache::SynchronizeBuffersInRanges(const RangeSet& ranges, uint64_t ranges_version) {
	KYTY_PROFILER_FUNCTION();
	// After one complete walk, a buffer can only need an upload if its pages became CPU-dirty
	// (logged by the tracker) or it was registered since. Take the log before uploading so that
	// writes racing with this call stay logged for the next one.
	// Every BDA shader may write untracked guest memory; host copies taken before it are stale.
	++m_bda_epoch;
	// Snapshot BEFORE taking the log. A write racing the take may stay in the next log, so
	// this call must not acknowledge its newer generation without publishing its bytes.
	const auto sync_generation = m_memory_tracker.CpuDirtyGeneration();
	const bool log_complete = m_memory_tracker.TakeCpuDirtyRanges(m_bda_dirty_ranges);
	const bool generation_enabled = BdaSyncGenerationEnabled();
	// Quiescent skip: nothing became CPU-dirty since the last walked call, no buffer was
	// registered and the mapped ranges are unchanged, so no buffer can need an upload. Correct
	// regardless of dirty-log completeness because any transition forces the walk below.
	bool oracle_dirty = false;
	if (generation_enabled && log_complete && m_bda_dirty_ranges.empty() &&
	    !m_bda_walk_required && m_bda_registered_ranges.empty() &&
	    sync_generation == m_bda_sync_generation && ranges_version == m_bda_ranges_version) {
		if (BdaSyncVerifyEnabled()) {
			if (m_memory_tracker.CpuDirtyGeneration() != sync_generation) {
				// A transition raced this call after the log was taken; force the walk so its
				// upload is published this call. Counted as raced, not missed.
				WaitStats::NoteBdaSync(3);
			} else if (const size_t hits = BdaSkipWouldMissUpload(ranges); hits == 0) {
				WaitStats::NoteBdaSync(0);
				return;
			} else {
				// Classify the oracle's hits AFTER its long scan: the generation was re-read
				// immediately before the scan, so a bump during the scan is a fresh concurrent
				// guest write racing this call (already logged for the next one), not a hidden
				// transition. Per-hit counting lives here so both outcomes use one generation view.
				const bool raced = m_memory_tracker.CpuDirtyGeneration() != sync_generation;
				for (size_t i = 0; i < hits; ++i) {
					WaitStats::NoteBdaSync(raced ? 3U : 2U);
				}
				if (!raced) {
					Log::WriteToConsoleAndLog(fmt::format(
					    "BDA gen-skip MISSED: hits={} (gen unchanged across oracle scan)\n", hits));
				}
				oracle_dirty = true;
			}
			// Else: the oracle found CPU-dirty pages the generation gate would have hidden.
			// Fall through to the conservative full walk.
		} else {
			WaitStats::NoteBdaSync(0);
			return;
		}
	}
	// Generation mode promises a conservative full walk after any transition, even when
	// the dirty log is enabled globally. A missing entry must never select the log-only path.
	if (!generation_enabled && BdaDirtyLogEnabled() && log_complete && !m_bda_walk_required &&
	    ranges_version == m_bda_ranges_version) {
		if (BdaSyncMergeEnabled()) {
			SynchronizeMergedBdaRanges(ranges);
			m_bda_registered_ranges.clear();
			WaitStats::NoteBdaSync(0);
			m_bda_sync_generation = sync_generation;
			if (m_bda_dirty_verify) {
				VerifyBdaDirtyLog(ranges);
			}
			return;
		}
		for (const auto* pending: {&m_bda_dirty_ranges, &m_bda_registered_ranges}) {
			for (const auto& range: *pending) {
				ranges.ForEachInRange(range.address, range.size, [&](uint64_t start, uint64_t end) {
					SynchronizeBuffersInRange(start, end - start);
				});
			}
		}
		m_bda_registered_ranges.clear();
		WaitStats::NoteBdaSync(0);
		m_bda_sync_generation = sync_generation;
		if (m_bda_dirty_verify) {
			VerifyBdaDirtyLog(ranges);
		}
		return;
	}
	WaitStats::NoteBdaSync(1);
	m_bda_registered_ranges.clear();
	m_bda_walk_required  = false;
	m_bda_ranges_version = ranges_version;
	m_bda_sync_generation = sync_generation;
	// Both collections are ordered and disjoint. Retain the cursor between mapped ranges
	// instead of starting a tree search (and a profiler zone) for every range on every draw.
	// A cache buffer can span a gap, so retain it until its end is consumed.
	auto it = m_buffers.begin();
	ranges.ForEach([&](uint64_t start, uint64_t end) {
		while (it != m_buffers.end()) {
			auto&      buffer     = m_slot_buffers[it->second];
			const auto buffer_end = buffer.CpuAddress() + buffer.Size();
			if (buffer_end <= start) {
				++it;
				continue;
			}
			if (buffer.CpuAddress() >= end) break;
			const auto begin  = std::max(buffer.CpuAddress(), start);
			const auto finish = std::min(buffer_end, end);
			(void)SynchronizeBuffer(buffer, begin, finish - begin, false, false);
			if (buffer_end > end) break;
			++it;
		}
	});
	if (oracle_dirty) {
		// Decisive post-walk recheck: after the forced walk, are the pages the oracle flagged
		// still CPU-dirty? If so and the generation did not change, the walk itself failed to
		// publish them (a real default-path upload miss), not just the generation gate.
		static std::atomic<uint32_t> postwalk_reports {0};
		if (postwalk_reports.fetch_add(1, std::memory_order_relaxed) < BdaVerifyReportLimit * 4) {
			const auto gen_post = m_memory_tracker.CpuDirtyGeneration();
			const auto still    = BdaCpuDirtyBufferCount(ranges, true);
			Log::WriteToConsoleAndLog(fmt::format(
			    "BDA postwalk: gen_pre={} gen_post={} still_dirty={}\n", sync_generation, gen_post,
			    still));
		}
	}
}

void BufferCache::SynchronizeBufferRangesOrdered(std::span<const GuestRange> ranges) {
	// Identical synchronization to SynchronizeBuffersInRanges' full walk, but over an already
	// address-sorted, coalesced range list: retain the m_buffers cursor between ranges instead of
	// one tree search per range. A buffer can span a gap, so keep it until its end is consumed.
	auto it = m_buffers.begin();
	for (const auto& range: ranges) {
		const auto start = range.address;
		const auto end   = start + range.size;
		while (it != m_buffers.end()) {
			auto&      buffer     = m_slot_buffers[it->second];
			const auto buffer_end = buffer.CpuAddress() + buffer.Size();
			if (buffer_end <= start) {
				++it;
				continue;
			}
			if (buffer.CpuAddress() >= end) {
				break;
			}
			const auto begin  = std::max(buffer.CpuAddress(), start);
			const auto finish = std::min(buffer_end, end);
			if (begin < finish) {
				(void)SynchronizeBuffer(buffer, begin, finish - begin, false, false);
			}
			if (buffer_end > end) {
				break;
			}
			++it;
		}
	}
}

void BufferCache::SynchronizeMergedBdaRanges(const RangeSet& ranges) {
	auto& merged = m_bda_merged_ranges;
	merged.clear();
	for (const auto* pending: {&m_bda_dirty_ranges, &m_bda_registered_ranges}) {
		for (const auto& range: *pending) {
			ranges.ForEachInRange(range.address, range.size, [&](uint64_t start, uint64_t end) {
				merged.push_back({start, end - start});
			});
		}
	}
	// Coalesce overlapping or adjacent ranges; ForEachUploadRange still uploads only dirty pages,
	// so widening a gap cannot upload clean data or miss a range.
	CoalesceRanges(merged);
	SynchronizeBufferRangesOrdered(merged);
}

void BufferCache::VerifyBdaDirtyLog(const RangeSet& ranges) {
	// Every logged page inside `ranges` was just uploaded, so a CPU-dirty page left inside a
	// BDA buffer is either a fault racing with this call (already in the next log) or a write
	// the log never saw. Count, report and repair the latter.
	auto it = m_buffers.begin();
	ranges.ForEach([&](uint64_t start, uint64_t end) {
		while (it != m_buffers.end()) {
			auto&      buffer     = m_slot_buffers[it->second];
			const auto buffer_end = buffer.CpuAddress() + buffer.Size();
			if (buffer_end <= start) {
				++it;
				continue;
			}
			if (buffer.CpuAddress() >= end) break;
			const auto begin  = std::max(buffer.CpuAddress(), start);
			const auto finish = std::min(buffer_end, end);
			if (m_memory_tracker.IsRegionCpuModified(begin, finish - begin)) {
				if (m_memory_tracker.CpuDirtyLogIntersects(begin, finish - begin)) {
					WaitStats::NoteBdaSync(3);
				} else {
					WaitStats::NoteBdaSync(2);
					if (WaitStats::BdaSyncOutcome(2) <= BdaVerifyReportLimit) {
						Log::WriteToConsoleAndLog(fmt::format(
						    "BDA dirty log MISSED: range 0x{:x}+0x{:x} buffer 0x{:x}+0x{:x}\n",
						    begin, finish - begin, buffer.CpuAddress(), buffer.Size()));
					}
					(void)SynchronizeBuffer(buffer, begin, finish - begin, false, false);
				}
			}
			if (buffer_end > end) break;
			++it;
		}
	});
}

size_t BufferCache::BdaSkipWouldMissUpload(const RangeSet& ranges) {
	// Exact oracle for the generation-gated quiescent skip: the full walk uploads exactly the
	// CPU-dirty pages of tracked buffers inside `ranges`, so a skip is safe iff none exist.
	// Returns the number of CPU-dirty spans found; the caller classifies them (missed vs raced)
	// after re-reading the generation, so a scan that races guest writes is not mislabeled here.
	size_t hits = 0;
	auto   it   = m_buffers.begin();
	ranges.ForEach([&](uint64_t start, uint64_t end) {
		while (it != m_buffers.end()) {
			auto&      buffer     = m_slot_buffers[it->second];
			const auto buffer_end = buffer.CpuAddress() + buffer.Size();
			if (buffer_end <= start) {
				++it;
				continue;
			}
			if (buffer.CpuAddress() >= end) break;
			const auto begin  = std::max(buffer.CpuAddress(), start);
			const auto finish = std::min(buffer_end, end);
			if (begin < finish &&
			    m_memory_tracker.IsRegionCpuModifiedExisting(begin, finish - begin)) {
				if (hits < BdaVerifyReportLimit) {
					Log::WriteToConsoleAndLog(fmt::format(
					    "BDA gen-skip DIRTY: range 0x{:x}+0x{:x} buffer 0x{:x}+0x{:x}\n", begin,
					    finish - begin, buffer.CpuAddress(), buffer.Size()));
				}
				++hits;
			}
			if (buffer_end > end) break;
			++it;
		}
	});
	return hits;
}

size_t BufferCache::BdaCpuDirtyBufferCount(const RangeSet& ranges, bool log) {
	// Post-walk recheck. Same iteration as BdaSkipWouldMissUpload, but counts instead of forcing a
	// walk. Non-zero after a complete walk means the walk did not clear/publish those pages.
	size_t count = 0;
	auto   it    = m_buffers.begin();
	ranges.ForEach([&](uint64_t start, uint64_t end) {
		while (it != m_buffers.end()) {
			auto&      buffer     = m_slot_buffers[it->second];
			const auto buffer_end = buffer.CpuAddress() + buffer.Size();
			if (buffer_end <= start) {
				++it;
				continue;
			}
			if (buffer.CpuAddress() >= end) break;
			const auto begin  = std::max(buffer.CpuAddress(), start);
			const auto finish = std::min(buffer_end, end);
			if (begin < finish &&
			    m_memory_tracker.IsRegionCpuModifiedExisting(begin, finish - begin)) {
				if (log && count < BdaVerifyReportLimit) {
					Log::WriteToConsoleAndLog(fmt::format(
					    "BDA postwalk DIRTY: range 0x{:x}+0x{:x} buffer 0x{:x}+0x{:x}\n", begin,
					    finish - begin, buffer.CpuAddress(), buffer.Size()));
				}
				++count;
			}
			if (buffer_end > end) break;
			++it;
		}
	});
	return count;
}

} // namespace Libs::Graphics
