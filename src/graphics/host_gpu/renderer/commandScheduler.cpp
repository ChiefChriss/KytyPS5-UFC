#include "graphics/host_gpu/renderer/commandScheduler.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/gpuCrashDiagnostics.h"

#include <algorithm>
#include <cstdio>
#include <optional>

namespace Libs::Graphics {

static thread_local CommandScheduler* g_deferred_callback_scheduler = nullptr;

namespace {

void ReportVulkanFatal(const char* what, vk::Result result, uint64_t tick, uint32_t debug_op,
                       uint64_t debug_submit, uint32_t arg0, uint32_t arg1, uint32_t arg2,
                       uint32_t arg3, uint64_t arg4) {
	LOGF("%s failed: %s (%d), tick=%" PRIu64 " debug_op=%u debug_submit=%" PRIu64
	     " args=%u,%u,%u,%u,0x%016" PRIx64 "\n",
	     what, vk::to_string(result).c_str(), static_cast<int>(result), tick, debug_op,
	     debug_submit, arg0, arg1, arg2, arg3, arg4);
	std::printf("%s failed: %s (%d), tick=%" PRIu64 " debug_op=%u debug_submit=%" PRIu64
	            " args=%u,%u,%u,%u,0x%016" PRIx64 "\n",
	            what, vk::to_string(result).c_str(), static_cast<int>(result), tick, debug_op,
	            debug_submit, arg0, arg1, arg2, arg3, arg4);
	std::fflush(stdout);
}

} // namespace

CommandScheduler::CommandPool::CommandPool(GraphicContext& graphics, MasterSemaphore& master)
    : m_graphics(graphics), m_master(master) {
	EXIT_IF(graphics.queue_family == static_cast<uint32_t>(-1));
	vk::CommandPoolCreateInfo create {};
	create.queueFamilyIndex = graphics.queue_family;
	create.flags            = vk::CommandPoolCreateFlagBits::eTransient |
	                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	const auto result       = graphics.device.createCommandPool(&create, nullptr, &m_pool);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess || m_pool == nullptr);
}

CommandScheduler::CommandPool::~CommandPool() {
	m_graphics.device.destroyCommandPool(m_pool, nullptr);
}

size_t CommandScheduler::CommandPool::Grow() {
	const auto first = m_ticks.size();
	m_ticks.resize(first + GrowStep);
	m_buffers.resize(first + GrowStep);

	vk::CommandBufferAllocateInfo allocate {};
	allocate.commandPool        = m_pool;
	allocate.level              = vk::CommandBufferLevel::ePrimary;
	allocate.commandBufferCount = static_cast<uint32_t>(GrowStep);
	EXIT_IF(m_graphics.device.allocateCommandBuffers(&allocate, m_buffers.data() + first) !=
	        vk::Result::eSuccess);
	return first;
}

vk::CommandBuffer CommandScheduler::CommandPool::Commit(uint64_t tick) {
	auto       gpu_tick = m_master.KnownGpuTick();
	const auto search   = [this, &gpu_tick, tick](size_t begin, size_t end) -> std::optional<size_t> {
		for (size_t index = begin; index < end; ++index) {
			if (gpu_tick >= m_ticks[index]) {
				m_ticks[index] = tick;
				return index;
			}
		}
		return std::nullopt;
	};

	auto found = search(m_hint, m_ticks.size());
	if (!found) {
		m_master.Refresh();
		gpu_tick = m_master.KnownGpuTick();
		found    = search(m_hint, m_ticks.size());
	}
	if (!found) {
		found = search(0, m_hint);
	}
	if (!found) {
		found           = Grow();
		m_ticks[*found] = tick;
	}

	m_hint = (*found + 1) % m_ticks.size();
	return m_buffers[*found];
}

bool CommandScheduler::InDeferredOperation() noexcept {
	return g_deferred_callback_scheduler != nullptr;
}

CommandScheduler::CommandScheduler(RenderContext& context, GraphicContext& graphics)
    : m_master(graphics), m_context(context), m_graphics(graphics),
      m_command_pool(graphics, m_master), m_command(*this),
      m_priority_thread([this](std::stop_token stop) { PriorityOperationsThread(stop); }) {}

CommandScheduler::~CommandScheduler() {
	Shutdown();
}

void CommandScheduler::Shutdown() {
	{
		std::unique_lock lock(m_operation_mutex);
		if (m_operation_state == OperationState::Closed) {
			return;
		}
		if (g_deferred_callback_scheduler == this) {
			EXIT_IF(m_operation_state == OperationState::Open);
			// A priority callback cannot join its own runner, while a normal callback can be
			// executing inside the shutdown owner's final PopPendingOperations. The owning
			// thread will finish shutdown after this callback returns.
			return;
		}
		if (m_operation_state == OperationState::Draining) {
			m_operation_available.wait(
			    lock, [this] { return m_operation_state == OperationState::Closed; });
			return;
		}
		m_operation_state = OperationState::Draining;
	}
	if (!m_command.IsInvalid()) {
		Submit();
	}
	m_master.Wait(CurrentTick() - 1);
	PopPendingOperations();
	DrainPriorityOperations();
	m_priority_thread.request_stop();
	m_operation_available.notify_all();
	if (m_priority_thread.joinable()) {
		m_priority_thread.join();
	}
	// Every tick was waited on above, so the queue thread has nothing left to submit.
	StopRecordingThread();
	StopSubmitThread();
	{
		std::lock_guard lock(m_operation_mutex);
		EXIT_IF(!m_pending_operations.empty() || !m_priority_operations.empty() ||
		        m_priority_active);
		m_operation_state = OperationState::Closed;
	}
	m_operation_available.notify_all();
}

void CommandScheduler::Begin(HW::Context& registers, HW::UserConfig& user_config,
                             HW::Shader& shaders) {
	{
		std::lock_guard lock(m_operation_mutex);
		EXIT_IF(m_operation_state != OperationState::Open);
	}
	m_command.Bind(registers, user_config, shaders);

	if (m_command.IsInvalid()) {
		BeginNext();
	}
}

void CommandScheduler::BeginRendering(const RenderState& state) {
	Current().BeginRendering(state);
}

void CommandScheduler::EndRendering() {
	if (Active() && !m_command.IsInvalid()) {
		Current().EndRendering();
	}
}

void CommandScheduler::Flush() {
	SubmitInfo submit;
	Flush(submit);
}

void CommandScheduler::Flush(SubmitInfo& submit) {
	Submit(submit);
	BeginNext();
}

void CommandScheduler::FlushAndWait() {
	KYTY_PROFILER_FUNCTION(profiler::colors::RedA100);
	const auto tick = Submit();
	m_master.Wait(tick);
	BeginNext();
}

void CommandScheduler::Finish() {
	KYTY_PROFILER_FUNCTION(profiler::colors::RedA100);
	CheckActive();
	if (!m_command.IsInvalid()) {
		Submit();
	}
	m_master.Wait(CurrentTick() - 1);
	BeginNext();
	PopPendingOperations();
}

void CommandScheduler::Wait(uint64_t tick) {
	EXIT_IF(tick > CurrentTick());
	if (tick == CurrentTick()) {
		CheckActive();
		// A stream-buffer wrap can wait while a draw is being prepared through a reference to
		// Current(). The wrapper stays stable while its pooled Vulkan buffer is retired. Deferred
		// resources are released only at the next GPU operation boundary.
		const auto submitted_tick = Submit();
		EXIT_IF(submitted_tick != tick);
		m_master.Wait(tick);
		BeginNext();
	} else {
		m_master.Wait(tick);
	}
}

void CommandScheduler::PopPendingOperations() {
	// Fast path: most draws/dispatches retire nothing. Skip the Vulkan timeline
	// counter query when no deferred retirement work is queued. KnownGpuTick is
	// monotonic, so skipping Refresh here can only defer a retirement to a later
	// call, never retire work early. Callers that need progress on a known tick
	// (CommandPool::Commit, IsFree, Wait) still refresh explicitly.
	{
		std::lock_guard lock(m_operation_mutex);
		if (m_pending_operations.empty()) {
			return;
		}
	}
	m_master.Refresh();
	for (;;) {
		PendingOperation operation;
		{
			std::lock_guard lock(m_operation_mutex);
			if (m_pending_operations.empty() ||
			    !m_master.IsFree(m_pending_operations.front().tick)) {
				return;
			}
			operation = std::move(m_pending_operations.front());
			m_pending_operations.pop();
		}
		WaitPriorityOperations(operation.tick);
		RunOperation(std::move(operation.callback));
	}
}

void CommandScheduler::DeferOperation(Common::UniqueFunction<void>&& operation) {
	CheckActive();
	EXIT_IF(!operation);
	std::unique_lock lock(m_operation_mutex);
	if (m_operation_state == OperationState::Open) {
		m_pending_operations.push({std::move(operation), CurrentTick()});
		return;
	}
	if (g_deferred_callback_scheduler == this) {
		lock.unlock();
		operation();
		return;
	}
	m_operation_available.wait(lock,
	                           [this] { return m_operation_state == OperationState::Closed; });
	lock.unlock();
	operation();
}

void CommandScheduler::DeferPriorityOperation(Common::UniqueFunction<void>&& operation) {
	CheckActive();
	EXIT_IF(!operation);
	std::unique_lock lock(m_operation_mutex);
	if (m_operation_state == OperationState::Open) {
		m_priority_operations.push({std::move(operation), CurrentTick()});
		lock.unlock();
		m_operation_available.notify_one();
		return;
	}
	if (g_deferred_callback_scheduler == this) {
		lock.unlock();
		operation();
		return;
	}
	m_operation_available.wait(lock,
	                           [this] { return m_operation_state == OperationState::Closed; });
	lock.unlock();
	operation();
}

void CommandScheduler::PriorityOperationsThread(std::stop_token stop) {
	while (!stop.stop_requested()) {
		PendingOperation operation;
		{
			std::unique_lock lock(m_operation_mutex);
			m_operation_available.wait(lock, [this, &stop] {
				return stop.stop_requested() || !m_priority_operations.empty();
			});
			if (stop.stop_requested()) {
				return;
			}
			operation = std::move(m_priority_operations.front());
			m_priority_operations.pop();
			m_priority_active      = true;
			m_priority_active_tick = operation.tick;
		}
		m_master.Wait(operation.tick);
		if (!stop.stop_requested()) {
			RunOperation(std::move(operation.callback));
		}
		{
			std::lock_guard lock(m_operation_mutex);
			m_priority_active      = false;
			m_priority_active_tick = 0;
		}
		m_operation_available.notify_all();
	}
}

void CommandScheduler::DrainPriorityOperations() {
	EXIT_IF(g_deferred_callback_scheduler == this);
	std::unique_lock lock(m_operation_mutex);
	m_operation_available.wait(
	    lock, [this] { return m_priority_operations.empty() && !m_priority_active; });
}

void CommandScheduler::WaitPriorityOperations(uint64_t tick) {
	EXIT_IF(g_deferred_callback_scheduler == this);
	std::unique_lock lock(m_operation_mutex);
	m_operation_available.wait(lock, [this, tick] {
		const bool active_before_or_at = m_priority_active && m_priority_active_tick <= tick;
		const bool queued_before_or_at =
		    !m_priority_operations.empty() && m_priority_operations.front().tick <= tick;
		return !active_before_or_at && !queued_before_or_at;
	});
}

void CommandScheduler::RunOperation(Common::UniqueFunction<void>&& operation) {
	auto* previous                = g_deferred_callback_scheduler;
	g_deferred_callback_scheduler = this;
	operation();
	g_deferred_callback_scheduler = previous;
}

bool CommandScheduler::IsFree(uint64_t tick) {
	if (m_master.IsFree(tick)) {
		return true;
	}
	m_master.Refresh();
	return m_master.IsFree(tick);
}

void CommandScheduler::CheckActive() const {
	EXIT_IF(!Active());
}

CommandBuffer& CommandScheduler::Current() {
	CheckActive();
	return m_command;
}

CommandBuffer& CommandScheduler::BeginCommand() {
	EXIT_IF(!m_command.IsInvalid());
	m_command.m_open = true;
	const auto tick = CurrentTick();
	if (m_threaded) {
		Record([this, tick](vk::CommandBuffer) {
			m_worker_buffer = m_command_pool.Commit(tick);
			vk::CommandBufferBeginInfo begin;
			begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
			EXIT_NOT_IMPLEMENTED(m_worker_buffer.begin(&begin) != vk::Result::eSuccess);
		});
		return m_command;
	}
	m_command.m_buffer = m_command_pool.Commit(tick);
	m_command.Begin();
	return m_command;
}

uint64_t CommandScheduler::Submit(SubmitInfo submit) {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(m_command.IsInvalid());
	EXIT_IF(submit.num_wait_semaphores > SubmitInfo::MaxSemaphores ||
	        submit.num_signal_semaphores >= SubmitInfo::MaxSemaphores);

	m_command.End();
	EXIT_IF(m_graphics.queue == nullptr);

	SubmitJob job {
	    .buffer       = m_command.m_buffer,
	    .submit       = submit,
	    .debug_op     = m_command.m_debug_op,
	    .debug_submit = m_command.m_debug_submit_id,
	    .debug_arg0   = m_command.m_debug_arg0,
	    .debug_arg1   = m_command.m_debug_arg1,
	    .debug_arg2   = m_command.m_debug_arg2,
	    .debug_arg3   = m_command.m_debug_arg3,
	    .debug_arg4   = m_command.m_debug_arg4,
	};
	m_command.m_buffer                = nullptr;
	m_command.m_open                  = false;
	if (m_threaded) {
		job.tick = m_master.NextTick();
		job.submit.AddSignal(m_master.Handle(), job.tick);
		Record([this, job](vk::CommandBuffer command) mutable {
			EXIT_NOT_IMPLEMENTED(command.end() != vk::Result::eSuccess);
			job.buffer = command;
			QueueSubmit(job);
			m_worker_buffer = nullptr;
		});
		DispatchChunk();
		return job.tick;
	}

	if (!m_async_submit) {
		QueueSubmit(job);
		return job.tick;
	}
	{
		// Ticks are allocated in queue order and the queue thread submits in that order, so the
		// timeline is signaled in order.
		std::lock_guard lock(m_submit_mutex);
		job.tick = m_master.NextTick();
		job.submit.AddSignal(m_master.Handle(), job.tick);
		m_submit_jobs.push_back(job);
	}
	m_submit_available.notify_one();
	return job.tick;
}

void CommandScheduler::QueueSubmit(SubmitJob& job) {
	auto&      graphics = m_graphics;
	vk::Result result;
	{
		Common::LockGuard lock(graphics.queue_mutex);
		if (!m_async_submit) {
			job.tick = m_master.NextTick();
			job.submit.AddSignal(m_master.Handle(), job.tick);
		}
		const auto& submit = job.submit;

		vk::TimelineSemaphoreSubmitInfo timeline_info {};
		timeline_info.waitSemaphoreValueCount   = submit.num_wait_semaphores;
		timeline_info.pWaitSemaphoreValues      = submit.wait_ticks.data();
		timeline_info.signalSemaphoreValueCount = submit.num_signal_semaphores;
		timeline_info.pSignalSemaphoreValues    = submit.signal_ticks.data();

		vk::SubmitInfo submit_info {};
		submit_info.pNext                = &timeline_info;
		submit_info.waitSemaphoreCount   = submit.num_wait_semaphores;
		submit_info.pWaitSemaphores      = submit.wait_semaphores.data();
		submit_info.pWaitDstStageMask    = submit.wait_stages.data();
		submit_info.commandBufferCount   = 1;
		submit_info.pCommandBuffers      = &job.buffer;
		submit_info.signalSemaphoreCount = submit.num_signal_semaphores;
		submit_info.pSignalSemaphores    = submit.signal_semaphores.data();

		result = graphics.queue.submit(1, &submit_info, nullptr);
	}

	if (result != vk::Result::eSuccess) {
		ReportVulkanFatal("vkQueueSubmit", result, job.tick, job.debug_op, job.debug_submit,
		                  job.debug_arg0, job.debug_arg1, job.debug_arg2, job.debug_arg3,
		                  job.debug_arg4);
		if (result == vk::Result::eErrorDeviceLost) {
			GpuCrashDiagnostics::ReportDeviceLost(graphics);
		}
	}
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess);
}

void CommandScheduler::EnableAsyncSubmit() {
	// The owner enables it while constructing, before any other thread can submit.
	EXIT_IF(m_async_submit);
	m_async_submit  = true;
	m_submit_thread = std::jthread([this](std::stop_token stop) { SubmitThread(stop); });
}

void CommandScheduler::SubmitThread(std::stop_token stop) {
	KYTY_PROFILER_THREAD("GpuQueueSubmit");
	for (;;) {
		SubmitJob job;
		{
			std::unique_lock lock(m_submit_mutex);
			// A stop request still submits the queued jobs: their ticks may already be waited on.
			if (!m_submit_available.wait(lock, stop, [this] { return !m_submit_jobs.empty(); })) {
				return;
			}
			job = m_submit_jobs.front();
			m_submit_jobs.pop_front();
		}
		QueueSubmit(job);
	}
}

void CommandScheduler::StopSubmitThread() {
	if (m_submit_thread.joinable()) {
		m_submit_thread.request_stop();
		m_submit_thread.join();
	}
	std::lock_guard lock(m_submit_mutex);
	EXIT_IF(!m_submit_jobs.empty());
}

void CommandScheduler::EnableRecordingThread() {
	EXIT_IF(m_threaded || m_async_submit || !m_command.IsInvalid());
	m_threaded = true;
	m_async_submit = true; // Submit assigns ticks before the recorder reaches QueueSubmit.
	m_record_thread = std::jthread([this](std::stop_token stop) { RecordingThread(stop); });
}

void* CommandScheduler::Allocate(size_t size, size_t alignment) {
	EXIT_IF(!m_threaded || alignment == 0 || alignment > alignof(std::max_align_t) ||
	        (alignment & (alignment - 1)) != 0 || size > SIZE_MAX - alignment);
	const auto take = [this, size, alignment] {
		std::lock_guard lock(m_record_mutex);
		if (size + alignment <= Chunk::DefaultSize && !m_free_chunks.empty()) {
			auto chunk = std::move(m_free_chunks.back());
			m_free_chunks.pop_back();
			return chunk;
		}
		return std::make_shared<Chunk>(std::max(Chunk::DefaultSize, size + alignment));
	};
	if (!m_chunk) m_chunk = take();
	auto offset = (m_chunk->used + alignment - 1) & ~(alignment - 1);
	if (size > m_chunk->Capacity() - offset) {
		DispatchChunk();
		m_chunk = take();
		offset = 0;
	}
	m_chunk->used = offset + size;
	return reinterpret_cast<uint8_t*>(m_chunk->storage.data()) + offset;
}

void CommandScheduler::DispatchChunk() {
	if (!m_chunk || !m_chunk->first) return;
	{
		std::lock_guard lock(m_record_mutex);
		m_record_queue.push_back(std::move(m_chunk));
		m_chunks_dispatched++;
	}
	m_record_available.notify_one();
}

vk::CommandBuffer CommandScheduler::DrainRecording() {
	if (!m_threaded) return m_command.m_buffer;
	DispatchChunk();
	std::unique_lock lock(m_record_mutex);
	m_record_drains++;
	m_record_executed.wait(lock, [this] { return m_chunks_executed == m_chunks_dispatched; });
	return m_worker_buffer;
}

void CommandScheduler::RecordingThread(std::stop_token stop) {
	KYTY_PROFILER_THREAD("GpuRecord");
	std::vector<std::shared_ptr<Chunk>> retired;
	for (;;) {
		std::shared_ptr<Chunk> chunk;
		{
			std::unique_lock lock(m_record_mutex);
			if (!m_record_available.wait(lock, stop, [this] { return !m_record_queue.empty(); })) return;
			chunk = std::move(m_record_queue.front());
			m_record_queue.pop_front();
		}
		for (auto* command = chunk->first; command;) {
			auto* next = command->next;
			command->execute(command, m_worker_buffer);
			command->destroy(command);
			command = next;
		}
		chunk->first = chunk->last = nullptr;
		chunk->owners.clear();
		retired.push_back(std::move(chunk));
		{
			std::lock_guard lock(m_record_mutex);
			for (auto it = retired.begin(); it != retired.end();) {
				// Later commands can still point into this chunk; only the last owner may recycle.
				if (it->use_count() == 1) {
					(*it)->used = 0;
					if ((*it)->Capacity() == Chunk::DefaultSize && m_free_chunks.size() < 32)
						m_free_chunks.push_back(std::move(*it));
					it = retired.erase(it);
				} else ++it;
			}
			m_chunks_executed++;
		}
		m_record_executed.notify_all();
	}
}

void CommandScheduler::StopRecordingThread() {
	if (!m_record_thread.joinable()) return;
	(void)DrainRecording();
	m_record_thread.request_stop();
	m_record_available.notify_all();
	m_record_thread.join();
	LOGF("Recording thread: %llu chunks, %llu raw-handle drains\n",
	     static_cast<unsigned long long>(m_chunks_dispatched),
	     static_cast<unsigned long long>(m_record_drains));
}

void CommandScheduler::BeginNext() {
	CheckActive();
	BeginCommand();
}

} // namespace Libs::Graphics
