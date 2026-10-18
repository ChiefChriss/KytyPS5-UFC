#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BUFFERCACHE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BUFFERCACHE_H_

#include "common/abi.h"
#include "common/common.h"
#include "common/lruCache.h"
#include "common/slotVector.h"
#include "graphics/host_gpu/memoryTracker.h"
#include "graphics/host_gpu/rangeSet.h"
#include "graphics/host_gpu/renderer/cache/faultManager.h"
#include "graphics/host_gpu/renderer/cache/multiLevelPageTable.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include <map>
#include <atomic>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace Libs::Graphics {

struct GraphicContext;
class CommandScheduler;
class TextureCache;

using BufferId = Common::SlotId;
inline constexpr BufferId NULL_BUFFER_ID {0};

class BufferCache {
public:
	static constexpr uint32_t CACHING_PAGEBITS = 14;
	static constexpr uint64_t CACHING_PAGESIZE = uint64_t {1} << CACHING_PAGEBITS;
	static constexpr uint64_t CACHING_NUMPAGES =
	    (LOWER_ADDRESS_SIZE + LibKernel::Memory::kExtendedMemorySize) >> CACHING_PAGEBITS;
	static constexpr uint64_t BDA_PAGETABLE_SIZE = CACHING_NUMPAGES * sizeof(vk::DeviceAddress);

	static constexpr uint64_t PageIndex(uint64_t address) {
		return (address < LOWER_ADDRESS_SIZE
		            ? address
		            : address - LibKernel::Memory::kExtendedMemoryBase + LOWER_ADDRESS_SIZE) >>
		       CACHING_PAGEBITS;
	}
	static constexpr uint64_t GuestAddress(uint64_t offset) {
		return offset < LOWER_ADDRESS_SIZE
		           ? offset
		           : offset - LOWER_ADDRESS_SIZE + LibKernel::Memory::kExtendedMemoryBase;
	}

	BufferCache(GraphicContext& graphics, CommandScheduler& scheduler, PageManager& page_manager,
	            TextureCache& texture_cache);
	~BufferCache();
	KYTY_CLASS_NO_COPY(BufferCache);

	void                   InvalidateMemory(uint64_t vaddr, uint64_t size);
	// `site` attributes the drain in wait diagnostics; Count derives it from `is_write`.
	void                   ReadMemory(uint64_t vaddr, uint64_t size, bool is_write = false,
	                                  WaitSite site = WaitSite::Count);
	[[nodiscard]] Buffer&  GetBuffer(BufferId id) { return m_slot_buffers[id]; }
	[[nodiscard]] BufferId FindBuffer(uint64_t vaddr, uint64_t size);
	// A BDA shader read unbacked pages in [vaddr, vaddr + size) (zero was returned): create the
	// owner and, with KYTY_UFC_BDA_KEEP=1, keep it from garbage collection.
	void HandleBdaFault(uint64_t vaddr, uint64_t size);
	[[nodiscard]] std::pair<Buffer*, uint64_t> ObtainBuffer(uint64_t vaddr, uint64_t size,
	                                                        bool     is_written,
	                                                        bool     is_texel_buffer = false,
	                                                        BufferId id              = {});
	[[nodiscard]] StreamBuffer&                GetUtilityBuffer(MemoryUsage usage) noexcept {
        switch (usage) {
            case MemoryUsage::Upload: return m_staging_buffer;
            case MemoryUsage::Stream: return m_stream_buffer;
            case MemoryUsage::Download: return m_download_buffer;
            case MemoryUsage::DeviceLocal: return m_device_buffer;
        }
        EXIT("BufferCache: invalid utility-buffer usage\n");
	}
	[[nodiscard]] const Buffer* GetGdsBuffer() const noexcept { return &m_gds_buffer; }
	// The first registration and the first consumer both clear the table, so unowned entries read
	// as zero (ported from upstream KytyPS5 180e65a, via Jetsku int15 f14e1554).
	[[nodiscard]] Buffer*       GetBdaPageTableBuffer();
	[[nodiscard]] Buffer* GetFaultBuffer() noexcept { return m_fault_manager.GetFaultBuffer(); }
	[[nodiscard]] std::pair<Buffer*, uint64_t> ObtainBufferForImage(uint64_t vaddr, uint64_t size);
	void FillBuffer(uint64_t vaddr, uint64_t size, uint32_t value, bool is_gds);
	// FillBuffer for guest memory that also publishes whole GPU-owned tracker pages to the guest
	// backing. Both copies then hold the fill, so later CPU reads need no GPU drain. Partial pages
	// and clean ranges use FillBuffer unchanged.
	void FillBufferCoherent(uint64_t vaddr, uint64_t size, uint32_t value);
	void CopyBuffer(uint64_t dst_vaddr, uint64_t src_vaddr, uint64_t size, bool dst_gds,
	                bool src_gds);
	// Cache-index and exact dirty-range queries require GPU-thread serialization.
	[[nodiscard]] bool IsRegionRegistered(uint64_t vaddr, uint64_t size);
	[[nodiscard]] bool HasGpuDirtyBytes(uint64_t vaddr, uint64_t size);
	[[nodiscard]] bool IsRegionCpuModified(uint64_t vaddr, uint64_t size);
	[[nodiscard]] bool IsRegionGpuModified(uint64_t vaddr, uint64_t size);
	void               ProcessFaultBuffer();
	void               SynchronizeBuffersInRange(uint64_t vaddr, uint64_t size);
	// `ranges_version` changes whenever `ranges` does; it forces a walk over every buffer.
	void               SynchronizeBuffersInRanges(const RangeSet& ranges, uint64_t ranges_version);
	void               RunGarbageCollector();
	// Guest frame counter, used to keep reused buffers alive across a frame's submissions.
	void SetGcFrame(uint64_t frame) noexcept { m_gc_frame = frame; }
	// GPU-written indirect arguments. The command processor watches the ranges it reads; after a
	// GPU operation writes one, RecordPendingArgsCopies (called after compute/DMA operations,
	// outside rendering) records a host copy. TryReadIndirectArgs returns that copy once its
	// submission completes, provided no GPU write or BDA shader followed it; otherwise the
	// caller reads guest memory, which drains the GPU as before.
	void               RecordPendingArgsCopies();
	[[nodiscard]] bool TryReadIndirectArgs(uint64_t vaddr, uint64_t size, void* out);
	// Starts watching a range without reading it (no wait), so later producers record host
	// copies; used when a draw is GPU-sourced but a deferred NUM_INSTANCES read may follow.
	void WatchIndirectArgs(uint64_t vaddr, uint64_t size);
	// GPU-sourced indirect draw arguments. When the range is GPU-owned, returns the native
	// buffer and offset holding the current contents without reading guest memory (no GPU
	// drain, no submit); a barrier orders earlier GPU writes before indirect-command reads.
	// Returns false when the CPU path must be used (clean range, bad alignment/size, ...).
	[[nodiscard]] bool TryObtainGpuIndirectArgs(uint64_t vaddr, uint64_t size, Buffer** out_buffer,
	                                            uint64_t* out_offset);
	// GPU-sourced indexed-indirect index binding. Binds the already-cached allocation
	// containing the guest index base (no new buffer is created here) from the base
	// address, synchronizing CPU-dirty pages over the allocation remainder so any
	// GPU-provided firstIndex/count stays within current data; a barrier orders earlier
	// GPU writes before index reads. Returns false when the CPU path must be used
	// (uncached base, oversized remainder, bad element size, ...).
	[[nodiscard]] bool TryObtainGpuIndexBinding(uint64_t base_vaddr, uint64_t element_size,
	                                            Buffer** out_buffer, uint64_t* out_offset,
	                                            uint64_t* out_span_size);
	// Diagnostics: GPU writers of a watched color-metadata range since the previous query, as a
	// mask (1 shader, 2 DMA fill, 4 DMA copy, 8 uniform-fill compute). Starts watching a range
	// on first query.
	[[nodiscard]] uint32_t TakeMetadataWriter(uint64_t vaddr, uint64_t size);
	// Diagnostics: the next GPU write of exactly this range comes from a uniform-fill compute.
	void NoteUniformFillDispatch(uint64_t vaddr, uint64_t size) noexcept {
		m_uniform_fill = {vaddr, size};
	}
	// Diagnostics: shader whose storage-buffer writes are being bound (0 when none).
	void SetWriterShader(uint64_t shader_hash) noexcept { m_writer_shader = shader_hash; }

private:
	struct WatchedWrite {
		uint64_t address = 0;
		uint64_t size    = 0;
		uint32_t writers = 0;
	};
	friend struct BufferCacheTestAccess;

	// Device memory is not zero-initialized, and a zero entry is what shaders read as unmapped:
	// before this, never-registered pages held garbage device addresses (random BDA reads/writes).
	void EnsureBdaPageTableInitialized();

	// Diagnostic for KYTY_UFC_BDA_DIRTY_VERIFY: see m_bda_dirty_verify.
	void VerifyBdaDirtyLog(const RangeSet& ranges);
	// Diagnostic for KYTY_UFC_BDA_SYNC_VERIFY: exact oracle for the generation-gated quiescent
	// BDA skip. Returns the number of CPU-dirty spans of tracked buffers inside `ranges`, so the
	// skip would hide an upload when it is non-zero. A full walk uploads exactly those pages, so
	// this is complete. The caller re-reads the generation after the scan and classifies each hit
	// as a genuine miss (generation unchanged) or a raced concurrent write (generation bumped).
	[[nodiscard]] size_t BdaSkipWouldMissUpload(const RangeSet& ranges);
	// Companion for the post-walk recheck: counts tracked buffers inside `ranges` that still have
	// CPU-dirty pages after the forced walk, logging each one when `log` is set. Side-effect-free
	// (never creates a region). A non-zero result after a walk that did not bump the generation
	// means the walk failed to publish those pages, not that the generation gate hid a transition.
	[[nodiscard]] size_t BdaCpuDirtyBufferCount(const RangeSet& ranges, bool log);

	bool IsBufferInvalid(BufferId id) const {
		const auto* buffer = m_slot_buffers.try_get(id);
		return buffer == nullptr || buffer->is_deleted;
	}

	using BufferMap = std::map<uint64_t, BufferId>;
	struct OverlapResult {
		BufferMap::iterator first;
		BufferMap::iterator last;
		uint64_t            begin;
		uint64_t            end;
		bool                has_stream_leap;
	};

	using PageTable = MultiLevelPageTable<BufferId, CACHING_PAGEBITS, 44, 20>;
	static_assert(CACHING_PAGESIZE == (uint64_t {1} << PageTable::kPageBits));
	void WriteDataBuffer(Buffer& buffer, uint64_t address, const void* source, uint64_t size);
	void TouchBuffer(const Buffer& buffer);
	[[nodiscard]] OverlapResult ResolveOverlaps(uint64_t vaddr, uint64_t size);
	void JoinOverlap(BufferId new_id, BufferId overlap_id, bool accumulate_stream_score);
	[[nodiscard]] BufferId CreateBuffer(uint64_t vaddr, uint64_t size);
	void                   Register(BufferId id);
	void                   Unregister(BufferId id);
	template <bool insert>
	void                     ChangeRegister(BufferId id);
	void                     DeleteBuffer(BufferId id);
	[[nodiscard]] bool       SynchronizeBuffer(Buffer& buffer, uint64_t vaddr, uint64_t size,
	                                           bool is_written, bool is_texel_buffer);
	[[nodiscard]] vk::Buffer UploadCopies(Buffer& buffer, std::span<vk::BufferCopy> copies,
	                                      uint64_t total_size);
	[[nodiscard]] bool SynchronizeBufferFromImage(Buffer& buffer, uint64_t vaddr, uint64_t size);
	// KYTY_UFC_BDA_SYNC_MERGE: synchronize an address-sorted, coalesced list of ranges with a
	// single retained cursor over m_buffers instead of one tree search per range.
	void SynchronizeBufferRangesOrdered(std::span<const GuestRange> ranges);
	void SynchronizeMergedBdaRanges(const RangeSet& ranges);
	// Synchronous downloads publish before returning; asynchronous callers wait before reuse.
	template <bool async>
	[[nodiscard]] bool DownloadBufferMemory(Buffer& buffer, uint64_t vaddr, uint64_t size);
	uint64_t ReadMemoryStep(uint64_t vaddr, uint64_t size, bool is_write, bool async);
	uint64_t PendingReadbackTick(uint64_t vaddr, uint64_t size) const;
	void ResolvePublishedReadbackPages(uint64_t vaddr, uint64_t size);

	GraphicContext&                                    m_graphics;
	CommandScheduler&                                  m_scheduler;
	FaultManager                                       m_fault_manager;
	Buffer                                             m_gds_buffer;
	Buffer                                             m_bda_pagetable_buffer;
	bool                                               m_bda_pagetable_initialized = false;
	Common::SlotVector<Buffer>                         m_slot_buffers;
	Common::LeastRecentlyUsedCache<BufferId, uint64_t> m_lru_cache;
	BufferMap                                          m_buffers;
	PageTable                                          m_page_table;
	RangeSet                                           m_gpu_modified_ranges;
	struct PendingReadback {
		uint64_t tick = 0;
		std::vector<GuestRange> ranges;
	};
	// Completion callbacks remove their own token, so overlapping downloads cannot
	// accidentally clear another download's publication dependency.
	mutable std::mutex m_readback_mutex;
	std::map<uint64_t, PendingReadback> m_pending_readbacks;
	RangeSet m_published_readbacks;
	uint64_t m_next_readback = 0;
	std::atomic<uint32_t> m_readback_waiters{0};
	// Guest pages a BDA shader faulted on (read as zero); with KYTY_UFC_BDA_KEEP, buffers over
	// them are never garbage-collected, including buffers re-created by overlap joins.
	RangeSet                                           m_bda_hot_ranges;
	MemoryTracker                                      m_memory_tracker;
	StreamBuffer                                       m_staging_buffer;
	StreamBuffer                                       m_stream_buffer;
	StreamBuffer                                       m_download_buffer;
	StreamBuffer                                       m_device_buffer;
	TextureCache&                                      m_texture_cache;
	Buffer                                             m_args_download;
	uint64_t                                           m_total_used_memory = 0;
	uint64_t m_trigger_gc_memory  = 1ull * 1024 * 1024 * 1024;
	uint64_t m_critical_gc_memory = 2ull * 1024 * 1024 * 1024;
	uint64_t m_gc_tick            = 0;
	uint64_t m_gc_frame           = 0;
	uint64_t m_gc_seen_frame      = 0;
	uint64_t m_gc_frame_tick      = 0;
	uint64_t m_gc_ticks_per_frame = 0;
	// KYTY_UFC_VRAM_GC_BUDGET=1: marks from the planning budget, re-read once per guest frame.
	uint64_t m_budget_frame    = UINT64_MAX;
	uint64_t m_budget_trigger  = 0;
	uint64_t m_budget_critical = 0;
	// Ranges that may need a BDA upload since the last SynchronizeBuffersInRanges call.
	std::vector<GuestRange> m_bda_registered_ranges;
	std::vector<GuestRange> m_bda_dirty_ranges;
	// Scratch for the KYTY_UFC_BDA_SYNC_MERGE fast path (GPU thread only).
	std::vector<GuestRange> m_bda_merged_ranges;
	uint64_t                m_bda_ranges_version = 0;
	bool                    m_bda_walk_required  = true;
	// KYTY_UFC_BDA_SYNC_GENERATION=1: generation of the last walked BDA call. A call skips the
	// walk only while the tracker reports no CPU-dirty transition since this value.
	uint64_t                m_bda_sync_generation = 0;
	// KYTY_UFC_BDA_DIRTY_VERIFY=1: after each dirty-log fast path, walk the BDA ranges, count
	// CPU-dirty pages the log missed and upload them (diagnostic; costs the full walk).
	bool                    m_bda_dirty_verify   = false;
	std::vector<WatchedWrite> m_watched_writes;
	uint32_t                  m_gpu_write_source = 0;
	GuestRange                m_uniform_fill {};
	struct ArgsWatch {
		uint64_t address   = 0;
		uint64_t size      = 0;
		uint64_t tick      = 0;
		uint64_t bda_epoch = 0;
		uint32_t slot      = 0;
		bool     pending   = false;
		bool     valid     = false;
	};
	std::vector<ArgsWatch> m_args_watches;
	bool                   m_args_pending = false;
	uint64_t               m_bda_epoch    = 0;
	uint64_t                  m_writer_shader = 0;
	uint32_t                  m_writer_logs   = 0;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_BUFFERCACHE_H_
