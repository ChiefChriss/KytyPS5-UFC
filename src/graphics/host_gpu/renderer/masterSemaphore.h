#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <atomic>

namespace Libs::Graphics {

struct GraphicContext;

// Diagnostic attribution of blocking timeline waits. Callers tag the reason on their own
// thread; MasterSemaphore::Wait accounts the blocked time and logs a rate-limited summary.
enum class WaitSite : uint8_t {
	Untagged,
	CpuWriteFlush,
	ColorMetadata,
	CpuReadFault,
	GpuThreadReadFault,
	FaultBuffer,
	DescriptorPool,
	StreamWrap,
	BufferGc,
	GuestSync,
	IndirectArgsCopy,
	SrtReadFault,
	IndirectReadFault,
	CpReadFault,
	Count,
};

// Attributes GPU-thread read faults inside the scope to `site` (inner scopes win; SRT
// materialization wins over both).
class ScopedFaultSite {
public:
	explicit ScopedFaultSite(WaitSite site) noexcept;
	~ScopedFaultSite();
	KYTY_CLASS_NO_COPY(ScopedFaultSite);
	// WaitSite::Count when no scope is active.
	[[nodiscard]] static WaitSite Current() noexcept;

private:
	WaitSite m_previous;
};

// Marks the current thread as materializing shader resources (SRT walk), so a read fault on
// GPU-written memory there is attributed to WaitSite::SrtReadFault.
class ScopedSrtEvaluation {
public:
	ScopedSrtEvaluation() noexcept;
	~ScopedSrtEvaluation();
	KYTY_CLASS_NO_COPY(ScopedSrtEvaluation);
	[[nodiscard]] static bool Active() noexcept;

private:
	bool m_previous;
};

class ScopedWaitSite {
public:
	explicit ScopedWaitSite(WaitSite site) noexcept;
	~ScopedWaitSite();
	KYTY_CLASS_NO_COPY(ScopedWaitSite);

private:
	WaitSite m_previous;
};

namespace WaitStats {
// Writers of a GPU-modified color-metadata range since its previous readback, as a bit mask:
// 1 shader, 2 DMA fill, 4 DMA copy, 8 uniform-fill compute; 0 is unknown/unwatched.
void NoteMetadataReadback(uint32_t writers, bool cleared) noexcept;
// Uniform-fill conversion outcome: 0 CPU fill, 1 GPU fill, 2 no color metadata, 3 size/alignment.
void NoteFillConversion(uint32_t outcome) noexcept;
// Buffer-cache churn: a cache buffer was created, or retired by the garbage collector.
void NoteBufferCreated() noexcept;
// Color-metadata slices evaluated on the GPU instead of a CPU readback.
void NoteMetadataGpuEvaluation(uint32_t slices) noexcept;
[[nodiscard]] uint64_t MetadataGpuEvaluations() noexcept;
// Indirect-argument read: 0 host copy used, 1 guest memory current, 2 copy stale, 3 copy not yet
// submitted, 4 newly watched, 5 GPU-sourced non-indexed draw, 6 GPU-sourced indexed draw,
// 7 indexed draw fell back to the CPU path; 8 multi element GPU-sourced, 9 multi element CPU;
// 10 deferred NUM_INSTANCES read from a host copy, 11 deferred NUM_INSTANCES read from guest memory.
void NoteIndirectArgs(uint32_t outcome) noexcept;
// Indirect-draw packet base-vertex / start-instance SGPR locations against the vertex shader's
// embedded-fetch offset SGPRs, per dimension: 0 same user-data slot, 1 neither uses one,
// 2 different (the CP would write a slot the shader does not read, or vice versa), 3 shader has
// no embedded fetch (offsets come from the host draw).
void NoteIndirectOffsetLocs(uint32_t vertex_class, uint32_t instance_class) noexcept;
[[nodiscard]] uint64_t IndirectOffsetLocs(uint32_t dimension, uint32_t cls) noexcept;
[[nodiscard]] uint64_t IndirectArgsOutcome(uint32_t outcome) noexcept;
void NoteBufferRetired() noexcept;
// BDA synchronization: 0 dirty-log fast path, 1 full walk, 2 verify found a CPU-dirty page the
// fast path left behind with no pending log entry (a missed upload, repaired), 3 verify found a
// dirty page that a racing fault has already logged for the next call.
void NoteBdaSync(uint32_t outcome) noexcept;
[[nodiscard]] uint64_t BdaSyncOutcome(uint32_t outcome) noexcept;
// SRT/resource materialization memo (KYTY_UFC_SRT_MEMO): 0 reused, 1 inputs changed,
// 2 a guest-memory read changed, 3 shadow recompute differed from the reusable result,
// 4 guest-memory reads replayed (adds `amount`).
void NoteSrtMemo(uint32_t outcome, uint64_t amount = 1) noexcept;
// BDA unbacked-page faults: 0 fault pages processed (each read returned zero), 1 BDA-resident
// buffers the garbage collector kept (KYTY_UFC_BDA_KEEP); 2 images freed by the texture GC.
void NoteBdaFaults(uint32_t outcome, uint64_t amount) noexcept;
[[nodiscard]] uint64_t SrtMemoOutcome(uint32_t outcome) noexcept;
// Native image creation churn, attributed to the acquisition decision that created it:
// 0 null image, 1 stencil association, 2 new acquire (no cached image), 3 layout expand/replace,
// 4 incompatible cached image replacement, 5 other.
void NoteImageCreate(uint32_t reason) noexcept;
[[nodiscard]] uint64_t ImageCreates(uint32_t reason) noexcept;
// Native-image backing pool (KYTY_UFC_IMAGE_POOL), default OFF: 0 recycled backing (pool hit),
// 1 fresh native image created (miss), 2 backing evicted to stay under the byte/count cap.
// A hit also adds `bytes` (the recycled allocation size) to the recycled-bytes total.
void NoteImagePool(uint32_t outcome, uint64_t bytes = 0) noexcept;
[[nodiscard]] uint64_t ImagePoolOutcome(uint32_t outcome) noexcept;
[[nodiscard]] uint64_t ImagePoolRecycledBytes() noexcept;
} // namespace WaitStats

class MasterSemaphore {
public:
	explicit MasterSemaphore(GraphicContext& graphics);
	~MasterSemaphore();
	KYTY_CLASS_NO_COPY(MasterSemaphore);

	[[nodiscard]] uint64_t CurrentTick() const noexcept {
		return m_current_tick.load(std::memory_order_acquire);
	}
	[[nodiscard]] uint64_t KnownGpuTick() const noexcept {
		return m_gpu_tick.load(std::memory_order_acquire);
	}
	[[nodiscard]] bool     IsFree(uint64_t tick) const noexcept { return KnownGpuTick() >= tick; }
	[[nodiscard]] uint64_t NextTick() noexcept {
		return m_current_tick.fetch_add(1, std::memory_order_release);
	}
	[[nodiscard]] vk::Semaphore Handle() const noexcept { return m_semaphore; }

	void Refresh();
	void Wait(uint64_t tick);

private:
	GraphicContext&       m_graphics;
	vk::Semaphore         m_semaphore = nullptr;
	std::atomic<uint64_t> m_gpu_tick {0};
	std::atomic<uint64_t> m_current_tick {1};
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_MASTERSEMAPHORE_H_
