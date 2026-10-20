#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include "common/assert.h"
#include "common/debugCounters.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/host_gpu/graphicContext.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string>

#include <fmt/format.h>

namespace Libs::Graphics {

namespace {

constexpr size_t WaitSiteCount = static_cast<size_t>(WaitSite::Count);
constexpr std::array<const char*, WaitSiteCount + 1> WaitSiteNames {
    "gpu_untagged", "cpu_write_flush", "color_metadata", "cpu_read_fault", "gpu_read_fault",
    "fault_buffer",
    "descriptor_pool", "stream_wrap", "buffer_gc", "guest_sync", "args_copy",
    "srt_read_fault", "indirect_read_fault", "cp_read_fault", "other_threads"};

thread_local WaitSite t_wait_site = WaitSite::Untagged;
thread_local bool     t_srt_evaluation = false;
thread_local WaitSite t_fault_site     = WaitSite::Count;

struct WaitCounters {
	std::array<std::atomic<uint64_t>, WaitSiteCount + 1> count {};
	std::array<std::atomic<uint64_t>, WaitSiteCount + 1> nanoseconds {};
	std::array<std::atomic<uint64_t>, 5>                 metadata_writer {};
	std::array<std::atomic<uint64_t>, 4>                 fill_conversion {};
	std::atomic<uint64_t>                                buffers_created {0};
	std::atomic<uint64_t>                                gpu_metadata_evaluations {0};
	std::array<std::atomic<uint64_t>, 12>                indirect_args {};
	std::array<std::atomic<uint64_t>, 8>                 indirect_locs {};
	std::array<std::atomic<uint64_t>, 4>                 bda_sync {};
	std::array<std::atomic<uint64_t>, 5>                 srt_memo {};
	std::array<std::atomic<uint64_t>, 3>                 bda_faults {};
	std::array<std::atomic<uint64_t>, 6>                 image_creates {};
	std::array<std::atomic<uint64_t>, 3>                 image_pool {};
	std::atomic<uint64_t>                                image_pool_recycled_bytes {0};
	std::atomic<uint64_t>                                buffers_retired {0};
	std::atomic<uint64_t>                                metadata_cleared {0};
	std::atomic<int64_t>                                 last_report_ns {0};
	std::mutex                                           report_mutex;
};

WaitCounters& Counters() {
	static WaitCounters counters;
	return counters;
}

int64_t NowNs() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
	           std::chrono::steady_clock::now().time_since_epoch())
	    .count();
}

void MaybeReport(int64_t now) {
	constexpr int64_t Interval = 30'000'000'000;
	auto&             counters = Counters();
	auto              last     = counters.last_report_ns.load(std::memory_order_relaxed);
	if (last == 0) {
		counters.last_report_ns.compare_exchange_strong(last, now, std::memory_order_relaxed);
		return;
	}
	if (now - last < Interval ||
	    !counters.last_report_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
		return;
	}
	std::scoped_lock lock(counters.report_mutex);
	std::string      text = "GPU waits (cumulative count/ms):";
	for (size_t site = 0; site <= WaitSiteCount; site++) {
		const auto count = counters.count[site].load(std::memory_order_relaxed);
		if (count == 0) {
			continue;
		}
		text += fmt::format(" {}={}/{:.1f}", WaitSiteNames[site], count,
		                    static_cast<double>(counters.nanoseconds[site].load(
		                        std::memory_order_relaxed)) /
		                        1e6);
	}
	const auto load = [](const std::atomic<uint64_t>& value) {
		return value.load(std::memory_order_relaxed);
	};
	text += fmt::format("; metadata readback writers shader={} dma_fill={} dma_copy={} "
	                    "uniform_fill_cs={} unknown={} cleared={}",
	                    load(counters.metadata_writer[0]), load(counters.metadata_writer[1]),
	                    load(counters.metadata_writer[2]), load(counters.metadata_writer[3]),
	                    load(counters.metadata_writer[4]), load(counters.metadata_cleared));
	text += fmt::format("; uniform fills cpu={} gpu={} not_metadata={} size_or_align={}",
	                    load(counters.fill_conversion[0]), load(counters.fill_conversion[1]),
	                    load(counters.fill_conversion[2]), load(counters.fill_conversion[3]));
	text += fmt::format("; buffers created={} gc_retired={}; gpu metadata slices={}",
	                    load(counters.buffers_created), load(counters.buffers_retired),
	                    load(counters.gpu_metadata_evaluations));
	text += fmt::format("; indirect args copy={} guest={} stale={} unsubmitted={} new={} "
	                    "gpu_indirect={} gpu_indexed={} idx_fallback={} multi_native={} multi_cpu={} "
	                    "lazy_copy={} lazy_guest={}",
	                    load(counters.indirect_args[0]), load(counters.indirect_args[1]),
	                    load(counters.indirect_args[2]), load(counters.indirect_args[3]),
	                    load(counters.indirect_args[4]), load(counters.indirect_args[5]),
	                    load(counters.indirect_args[6]), load(counters.indirect_args[7]),
	                    load(counters.indirect_args[8]), load(counters.indirect_args[9]),
	                    load(counters.indirect_args[10]), load(counters.indirect_args[11]));
	text += fmt::format("; indirect sgpr locs vertex match={} none={} mismatch={} no_fetch={} "
	                    "instance match={} none={} mismatch={} no_fetch={}\n",
	                    load(counters.indirect_locs[0]), load(counters.indirect_locs[1]),
	                    load(counters.indirect_locs[2]), load(counters.indirect_locs[3]),
	                    load(counters.indirect_locs[4]), load(counters.indirect_locs[5]),
	                    load(counters.indirect_locs[6]), load(counters.indirect_locs[7]));
	text.pop_back();
	text += fmt::format("; bda sync fast={} walk={} verify_missed={} verify_raced={}",
	                    load(counters.bda_sync[0]), load(counters.bda_sync[1]),
	                    load(counters.bda_sync[2]), load(counters.bda_sync[3]));
	text += fmt::format("; srt memo reused={} inputs={} reads={} shadow_mismatch={} "
	                    "replayed_reads={}\n",
	                    load(counters.srt_memo[0]), load(counters.srt_memo[1]),
	                    load(counters.srt_memo[2]), load(counters.srt_memo[3]),
	                    load(counters.srt_memo[4]));
	text.pop_back();
	text += fmt::format("; bda fault pages={} gc_kept={}; images gc_freed={}\n",
	                    load(counters.bda_faults[0]), load(counters.bda_faults[1]),
	                    load(counters.bda_faults[2]));
	text.pop_back();
	text += fmt::format("; image creates null={} stencil={} acquire={} expand={} replace={} "
	                    "other={}\n",
	                    load(counters.image_creates[0]), load(counters.image_creates[1]),
	                    load(counters.image_creates[2]), load(counters.image_creates[3]),
	                    load(counters.image_creates[4]), load(counters.image_creates[5]));
	text.pop_back();
	text += fmt::format("; image pool hits={} misses={} evict={} recycled_mb={:.1f}\n",
	                    load(counters.image_pool[0]), load(counters.image_pool[1]),
	                    load(counters.image_pool[2]),
	                    static_cast<double>(load(counters.image_pool_recycled_bytes)) /
	                        (1024.0 * 1024.0));
	Log::WriteToConsoleAndLog(text);
}

} // namespace

ScopedFaultSite::ScopedFaultSite(WaitSite site) noexcept: m_previous(t_fault_site) {
	t_fault_site = site;
}

ScopedFaultSite::~ScopedFaultSite() {
	t_fault_site = m_previous;
}

WaitSite ScopedFaultSite::Current() noexcept {
	return t_fault_site;
}

ScopedSrtEvaluation::ScopedSrtEvaluation() noexcept: m_previous(t_srt_evaluation) {
	t_srt_evaluation = true;
}

ScopedSrtEvaluation::~ScopedSrtEvaluation() {
	t_srt_evaluation = m_previous;
}

bool ScopedSrtEvaluation::Active() noexcept {
	return t_srt_evaluation;
}

ScopedWaitSite::ScopedWaitSite(WaitSite site) noexcept: m_previous(t_wait_site) {
	t_wait_site = site;
}

ScopedWaitSite::~ScopedWaitSite() {
	t_wait_site = m_previous;
}

void WaitStats::NoteBufferCreated() noexcept {
	Counters().buffers_created.fetch_add(1, std::memory_order_relaxed);
}

void WaitStats::NoteMetadataGpuEvaluation(uint32_t slices) noexcept {
	Counters().gpu_metadata_evaluations.fetch_add(slices, std::memory_order_relaxed);
}

void WaitStats::NoteBdaSync(uint32_t outcome) noexcept {
	Counters().bda_sync[outcome < 4 ? outcome : 1].fetch_add(1, std::memory_order_relaxed);
}

uint64_t WaitStats::BdaSyncOutcome(uint32_t outcome) noexcept {
	return outcome < 4 ? Counters().bda_sync[outcome].load(std::memory_order_relaxed) : 0;
}

void WaitStats::NoteBdaFaults(uint32_t outcome, uint64_t amount) noexcept {
	Counters().bda_faults[outcome < 3 ? outcome : 0].fetch_add(amount, std::memory_order_relaxed);
}

void WaitStats::NoteSrtMemo(uint32_t outcome, uint64_t amount) noexcept {
	Counters().srt_memo[outcome < 5 ? outcome : 1].fetch_add(amount, std::memory_order_relaxed);
}

uint64_t WaitStats::SrtMemoOutcome(uint32_t outcome) noexcept {
	return outcome < 5 ? Counters().srt_memo[outcome].load(std::memory_order_relaxed) : 0;
}

void WaitStats::NoteImageCreate(uint32_t reason) noexcept {
	Counters().image_creates[reason < 6 ? reason : 5].fetch_add(1, std::memory_order_relaxed);
}

uint64_t WaitStats::ImageCreates(uint32_t reason) noexcept {
	return reason < 6 ? Counters().image_creates[reason].load(std::memory_order_relaxed) : 0;
}

void WaitStats::NoteImagePool(uint32_t outcome, uint64_t bytes) noexcept {
	auto& counters = Counters();
	counters.image_pool[outcome < 3 ? outcome : 2].fetch_add(1, std::memory_order_relaxed);
	if (outcome == 0 && bytes != 0) {
		counters.image_pool_recycled_bytes.fetch_add(bytes, std::memory_order_relaxed);
	}
}

uint64_t WaitStats::ImagePoolOutcome(uint32_t outcome) noexcept {
	return outcome < 3 ? Counters().image_pool[outcome].load(std::memory_order_relaxed) : 0;
}

uint64_t WaitStats::ImagePoolRecycledBytes() noexcept {
	return Counters().image_pool_recycled_bytes.load(std::memory_order_relaxed);
}

void WaitStats::NoteIndirectArgs(uint32_t outcome) noexcept {
	Counters().indirect_args[outcome < 12 ? outcome : 2].fetch_add(1, std::memory_order_relaxed);
}

uint64_t WaitStats::IndirectArgsOutcome(uint32_t outcome) noexcept {
	return outcome < 12 ? Counters().indirect_args[outcome].load(std::memory_order_relaxed) : 0;
}

void WaitStats::NoteIndirectOffsetLocs(uint32_t vertex_class, uint32_t instance_class) noexcept {
	auto& locs = Counters().indirect_locs;
	locs[vertex_class < 4 ? vertex_class : 2].fetch_add(1, std::memory_order_relaxed);
	locs[4 + (instance_class < 4 ? instance_class : 2)].fetch_add(1, std::memory_order_relaxed);
}

uint64_t WaitStats::IndirectOffsetLocs(uint32_t dimension, uint32_t cls) noexcept {
	return dimension < 2 && cls < 4
	           ? Counters().indirect_locs[dimension * 4 + cls].load(std::memory_order_relaxed)
	           : 0;
}

uint64_t WaitStats::MetadataGpuEvaluations() noexcept {
	return Counters().gpu_metadata_evaluations.load(std::memory_order_relaxed);
}

void WaitStats::NoteBufferRetired() noexcept {
	Counters().buffers_retired.fetch_add(1, std::memory_order_relaxed);
}

void WaitStats::NoteFillConversion(uint32_t outcome) noexcept {
	Counters().fill_conversion[outcome < 4 ? outcome : 3].fetch_add(1, std::memory_order_relaxed);
}

void WaitStats::NoteMetadataReadback(uint32_t writers, bool cleared) noexcept {
	auto& counters = Counters();
	for (uint32_t bit = 0; bit < 4; bit++) {
		if ((writers & (1u << bit)) != 0) {
			counters.metadata_writer[bit].fetch_add(1, std::memory_order_relaxed);
		}
	}
	if (writers == 0) {
		counters.metadata_writer[4].fetch_add(1, std::memory_order_relaxed);
	}
	if (cleared) {
		counters.metadata_cleared.fetch_add(1, std::memory_order_relaxed);
	}
}

MasterSemaphore::MasterSemaphore(GraphicContext& graphics): m_graphics(graphics) {
	vk::SemaphoreTypeCreateInfo type_info {};
	type_info.semaphoreType = vk::SemaphoreType::eTimeline;
	type_info.initialValue  = 0;

	vk::SemaphoreCreateInfo create_info {};
	create_info.pNext = &type_info;

	const auto result = m_graphics.device.createSemaphore(&create_info, nullptr, &m_semaphore);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess || m_semaphore == nullptr);
}

MasterSemaphore::~MasterSemaphore() {
	if (m_semaphore != nullptr) {
		m_graphics.device.destroySemaphore(m_semaphore, nullptr);
	}
}

void MasterSemaphore::Refresh() {
	uint64_t   counter = 0;
	const auto result  = m_graphics.device.getSemaphoreCounterValue(m_semaphore, &counter);
	if (result != vk::Result::eSuccess) {
		EXIT("Vulkan timeline counter query failed: %s (%d)\n",
		     vk::to_string(result).c_str(), static_cast<int>(result));
	}

	auto known = m_gpu_tick.load(std::memory_order_acquire);
	while (known < counter &&
	       !m_gpu_tick.compare_exchange_weak(known, counter, std::memory_order_release,
	                                         std::memory_order_relaxed)) {
	}
}

void MasterSemaphore::Wait(uint64_t tick) {
	if (IsFree(tick)) {
		return;
	}
	Refresh();
	if (IsFree(tick)) {
		return;
	}
	KYTY_PROFILER_BLOCK("MasterSemaphore::Wait", profiler::colors::RedA100);
	const auto wait_begin = NowNs();

	vk::SemaphoreWaitInfo wait_info {};
	wait_info.semaphoreCount = 1;
	wait_info.pSemaphores    = &m_semaphore;
	wait_info.pValues        = &tick;

	Common::DebugCounters::ScopedTimer timer(Common::DebugCounters::Counter::GpuWaits,
	                                         Common::DebugCounters::Counter::GpuWaitNs);
	const auto result = m_graphics.device.waitSemaphores(&wait_info, UINT64_MAX);
	if (result != vk::Result::eSuccess) {
		EXIT("Vulkan timeline wait failed: %s (%d), requested_tick=%llu known_tick=%llu\n",
		     vk::to_string(result).c_str(), static_cast<int>(result),
		     static_cast<unsigned long long>(tick),
		     static_cast<unsigned long long>(m_gpu_tick.load(std::memory_order_acquire)));
	}
	Refresh();

	const auto wait_end = NowNs();
	const auto site     = GuestGpu::IsGpuThread() ? static_cast<size_t>(t_wait_site) : WaitSiteCount;
	auto&      counters = Counters();
	counters.count[site].fetch_add(1, std::memory_order_relaxed);
	counters.nanoseconds[site].fetch_add(static_cast<uint64_t>(wait_end - wait_begin),
	                                     std::memory_order_relaxed);
	MaybeReport(wait_end);
}

} // namespace Libs::Graphics
