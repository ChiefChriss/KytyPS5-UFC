#include "graphics/host_gpu/renderer/renderContext.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/guest_gpu/graphicsRun.h"
#include "graphics/presentation/videoOut.h"
#include "graphics/shader/recompiler/NanTrace.h"
#include "libs/errno.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fmt/format.h>

namespace Libs::Graphics {

RenderContext::RenderContext(GraphicContext& graphics)
    : m_graphics(graphics), m_render_executor(*this), m_command_scheduler(*this, graphics),
      m_descriptor_heap(graphics, m_command_scheduler.GetMasterSemaphore()),
      m_pipeline_cache(graphics), m_sampler_cache(graphics),
      m_buffer_cache(graphics, m_command_scheduler, m_page_manager, m_texture_cache),
      m_texture_cache(graphics, m_command_scheduler, m_page_manager, m_buffer_cache) {
	EXIT_NOT_IMPLEMENTED(!Common::Thread::IsMainThread());
	// vkQueueSubmit, and the queue lock that present also holds, runs on a queue thread instead
	// of the GPU thread. Frames reach the presenter only after their flip tick completed, and the
	// presenter's own scheduler stays synchronous. KYTY_ASYNC_SUBMIT=0 submits on the GPU thread.
	const char* async_submit = std::getenv("KYTY_ASYNC_SUBMIT");
	const char* record_thread = std::getenv("KYTY_RECORD_THREAD");
	if (record_thread == nullptr || std::strcmp(record_thread, "0") != 0) {
		m_command_scheduler.EnableRecordingThread();
	} else if (async_submit == nullptr || std::strcmp(async_submit, "0") != 0) {
		m_command_scheduler.EnableAsyncSubmit();
	}
	LOGF("Async queue submit: %s\n", m_command_scheduler.AsyncSubmit() ? "on" : "off");
	LOGF("Vulkan recording thread: %s\n", m_command_scheduler.Threaded() ? "on" : "off");
	// KYTY_UFC_NAN_TRACE=1: a host-visible buffer the translated shaders stamp when a write site
	// first produces NaN or Inf. Created before any shader is translated.
	if (ShaderRecompiler::NanTrace::Requested()) {
		m_nan_trace_buffer = std::make_unique<Buffer>(
		    graphics, m_command_scheduler, MemoryUsage::Download, 0,
		    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress |
		        vk::BufferUsageFlagBits::eTransferSrc,
		    ShaderRecompiler::NanTrace::BufferBytes);
		const auto mapped = m_nan_trace_buffer->Mapped();
		EXIT_IF(mapped.size() < ShaderRecompiler::NanTrace::BufferBytes);
		std::memset(mapped.data(), 0, static_cast<size_t>(ShaderRecompiler::NanTrace::BufferBytes));
		m_nan_trace_buffer->Flush(0, ShaderRecompiler::NanTrace::BufferBytes);
		m_nan_trace_snapshot = std::make_unique<NanTraceSnapshot>(graphics, m_command_scheduler);
		ShaderRecompiler::NanTrace::Activate(m_nan_trace_buffer->BufferDeviceAddress());
	}
}

RenderContext::~RenderContext() {
	ShutdownGpu();
	m_command_scheduler.Shutdown();
	ShaderRecompiler::NanTrace::Deactivate();
}

void RenderContext::InitializeGpu(VideoOut::VideoOutDriver* video_out) {
	EXIT_IF(m_gpu != nullptr);
	m_video_out = video_out;
	m_gpu       = std::make_unique<GuestGpu>(*this);
}

void RenderContext::ShutdownGpu() {
	if (m_gpu != nullptr) {
		m_gpu->Shutdown();
		m_gpu.reset();
	}
	if (m_video_out != nullptr) {
		if (m_command_scheduler.Active()) {
			m_command_scheduler.Finish();
		}
		m_command_scheduler.DrainPriorityOperations();
		m_video_out = nullptr;
	}
}

GuestGpu& RenderContext::GetGpu() const {
	EXIT_IF(m_gpu == nullptr);
	return *m_gpu;
}

VideoOut::VideoOutDriver& RenderContext::GetVideoOut() const {
	EXIT_IF(m_video_out == nullptr);
	return *m_video_out;
}

bool RenderContext::HandleFault(PageFaultAccess access, uint64_t fault_vaddr) noexcept {
	// The host reports the faulting byte, not the instruction's access width. Both caches
	// resolve its page; guessing a width can cross the end of a valid guest mapping.
	constexpr uint64_t fault_size = 1;
	if (!IsMapped(fault_vaddr, fault_size)) {
		return false;
	}
	if (access == PageFaultAccess::Write) {
		m_buffer_cache.InvalidateMemory(fault_vaddr, fault_size);
		m_texture_cache.InvalidateMemory(fault_vaddr, fault_size);
	} else {
		// The command processor reads GPU-written indirect arguments on the GPU thread.
		auto site = GuestGpu::IsGpuThread() ? WaitSite::GpuThreadReadFault : WaitSite::CpuReadFault;
		if (ScopedSrtEvaluation::Active()) {
			site = WaitSite::SrtReadFault;
		} else if (const auto scoped = ScopedFaultSite::Current(); scoped != WaitSite::Count) {
			site = scoped;
		}
		m_buffer_cache.ReadMemory(fault_vaddr, fault_size, false, site);
	}
	return true;
}

bool RenderContext::InvalidateMemory(uint64_t vaddr, uint64_t size) {
	if (!IsMapped(vaddr, size)) {
		// A host write (e.g. a file read) that straddles the edge of GPU-mapped memory used to
		// skip invalidation entirely, leaving the GPU-mapped part write-protected and its GPU
		// copies stale. Invalidate the mapped parts.
		const auto bytes = InvalidateMappedOverlap(vaddr, size, true);
		if (bytes != 0) {
			static std::atomic<uint32_t> reported {0};
			if (reported.fetch_add(1, std::memory_order_relaxed) < 32) {
				Log::WriteToConsoleAndLog(fmt::format(
				    "Partial GPU-memory invalidation: addr=0x{:x} size=0x{:x} mapped=0x{:x}\n",
				    vaddr, size, bytes));
			}
		}
		return bytes != 0;
	}
	m_buffer_cache.InvalidateMemory(vaddr, size);
	m_texture_cache.InvalidateMemory(vaddr, size);
	return true;
}

uint64_t RenderContext::InvalidateMappedOverlap(uint64_t vaddr, uint64_t size, bool invalidate) {
	if (!GuestRange {vaddr, size}.Valid()) {
		return 0;
	}
	std::vector<GuestRange> parts;
	{
		std::shared_lock lock(m_mapped_ranges_mutex);
		m_mapped_ranges.ForEachInRange(vaddr, size, [&](uint64_t start, uint64_t end) {
			parts.push_back({start, end - start});
		});
	}
	uint64_t bytes = 0;
	for (const auto& part: parts) {
		bytes += part.size;
		if (invalidate) {
			m_buffer_cache.InvalidateMemory(part.address, part.size);
			m_texture_cache.InvalidateMemory(part.address, part.size);
		}
	}
	return bytes;
}

bool RenderContext::IsMapped(uint64_t vaddr, uint64_t size) const noexcept {
	if (!GuestRange {vaddr, size}.Valid()) {
		return false;
	}
	std::shared_lock lock(m_mapped_ranges_mutex);
	return m_mapped_ranges.Contains(vaddr, size);
}

void RenderContext::MapMemory(uint64_t vaddr, uint64_t size) {
	std::lock_guard lock(m_mapped_ranges_mutex);
	m_mapped_ranges.Add(vaddr, size);
	++m_mapped_ranges_version;
}

void RenderContext::UnmapMemory(uint64_t vaddr, uint64_t size) {
	if (CommandScheduler::InDeferredOperation()) {
		EXIT("unsupported memory unmap from an asynchronous GPU completion, "
		     "addr=0x%016" PRIx64 " size=0x%016" PRIx64 "\n",
		     vaddr, size);
	}
	const auto unmap = [this, vaddr, size] {
		if (m_command_scheduler.Active()) {
			const auto tick = m_command_scheduler.CurrentTick();
			m_command_scheduler.Finish();
			m_command_scheduler.WaitPriorityOperations(tick);
		}
		m_buffer_cache.InvalidateMemory(vaddr, size);
		m_texture_cache.UnmapMemory(vaddr, size);
		std::lock_guard lock(m_mapped_ranges_mutex);
		m_mapped_ranges.Subtract(vaddr, size);
		++m_mapped_ranges_version;
	};
	// Shutdown still owns the GPU while queued rendering drains, but its command lane no
	// longer accepts external work. Use the guest GPU's state for the teardown route.
	if (m_gpu == nullptr || m_gpu->IsStopping()) {
		unmap();
		return;
	}
	m_gpu->SendCommandSync(unmap);
}

void RenderContext::PrepareBda() {
	if (!m_bda_logged) {
		Log::WriteToConsoleAndLog("GPU: using buffer device address (BDA) shader memory access.\n");
		m_bda_logged = true;
	}
	std::shared_lock lock(m_mapped_ranges_mutex);
	m_buffer_cache.SynchronizeBuffersInRanges(m_mapped_ranges, m_mapped_ranges_version);
	m_fault_process_pending = true;
}

void RenderContext::RunGarbageCollector() {
	if (m_fault_process_pending) {
		m_fault_process_pending = false;
		m_buffer_cache.ProcessFaultBuffer();
	}
	m_texture_cache.ProcessDownloadImages();
	if (m_gpu != nullptr) {
		m_texture_cache.SetGcFrame(static_cast<uint64_t>(m_gpu->GetFrameNum()));
		m_buffer_cache.SetGcFrame(static_cast<uint64_t>(m_gpu->GetFrameNum()));
	}
	m_texture_cache.RunGarbageCollector();
	m_buffer_cache.RunGarbageCollector();
	if (m_nan_trace_buffer != nullptr && m_gpu != nullptr) {
		const auto frame = static_cast<uint64_t>(m_gpu->GetFrameNum());
		if (frame != m_nan_trace_frame) {
			m_nan_trace_frame = frame;
			if (auto snapshot = m_nan_trace_snapshot->Consume()) {
				ShaderRecompiler::NanTrace::Poll(snapshot->frame, snapshot->tick, snapshot->words);
			}
			if (m_command_scheduler.Active()) {
				m_nan_trace_snapshot->Record(*m_nan_trace_buffer, frame);
			}
		}
	}
}

void RenderContext::AddInterruptEq(LibKernel::EventQueue::KernelEqueue eq, int event_id) {
	Common::LockGuard lock(m_interrupt_mutex);

	auto it = std::find_if(
	    m_interrupt_eqs.begin(), m_interrupt_eqs.end(),
	    [eq, event_id](const auto& entry) { return entry.eq == eq && entry.event_id == event_id; });
	if (it != m_interrupt_eqs.end()) {
		return;
	}

	m_interrupt_eqs.push_back({eq, event_id});
}

void RenderContext::DeleteInterruptEq(LibKernel::EventQueue::KernelEqueue eq, int event_id) {
	Common::LockGuard lock(m_interrupt_mutex);

	auto it = std::find_if(
	    m_interrupt_eqs.begin(), m_interrupt_eqs.end(),
	    [eq, event_id](const auto& entry) { return entry.eq == eq && entry.event_id == event_id; });
	if (it == m_interrupt_eqs.end()) {
		return;
	}

	m_interrupt_eqs.erase(it);
}

void RenderContext::TriggerInterrupt(int event_id, uint32_t context_id) {
	std::vector<InterruptEqRegistration> registrations;
	{
		Common::LockGuard lock(m_interrupt_mutex);
		for (const auto& registration: m_interrupt_eqs) {
			if (registration.event_id == event_id) {
				registrations.push_back(registration);
			}
		}
	}

	for (const auto& registration: registrations) {
		const auto result = LibKernel::EventQueue::KernelTriggerEvent(
		    registration.eq, static_cast<uintptr_t>(registration.event_id),
		    LibKernel::EventQueue::KERNEL_EVFILT_GRAPHICS,
		    reinterpret_cast<void*>(static_cast<uintptr_t>(context_id)));
		if (result == LibKernel::KERNEL_ERROR_EBADF || result == LibKernel::KERNEL_ERROR_ENOENT) {
			DeleteInterruptEq(registration.eq, registration.event_id);
			continue;
		}
		EXIT_NOT_IMPLEMENTED(result != OK);
	}
}

} // namespace Libs::Graphics
