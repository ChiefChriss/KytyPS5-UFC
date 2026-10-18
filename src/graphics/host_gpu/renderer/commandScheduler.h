#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_COMMANDSCHEDULER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_COMMANDSCHEDULER_H_

#include "common/common.h"
#include "common/uniqueFunction.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"
#include "graphics/host_gpu/renderer/render.h"

#include <condition_variable>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>

#include <queue>

#include <thread>
#include <vector>

namespace Libs::Graphics {

class CommandScheduler {
public:
	CommandScheduler(RenderContext& context, GraphicContext& graphics);
	~CommandScheduler();
	KYTY_CLASS_NO_COPY(CommandScheduler);

	void           Begin(HW::Context& registers, HW::UserConfig& user_config, HW::Shader& shaders);
	void           BeginRendering(const RenderState& state);
	void           EndRendering();
	void           Flush();
	void           Flush(SubmitInfo& submit);
	void           FlushAndWait();
	void           Finish();
	CommandBuffer& BeginCommand();
	uint64_t       Submit(SubmitInfo submit = {});
	// Deferred callbacks can observe an externally owned drain, but cannot initiate shutdown:
	// the priority runner cannot join itself.
	void                      Shutdown();
	void                      Wait(uint64_t tick);
	void                      PopPendingOperations();
	void                      DrainPriorityOperations();
	void                      WaitPriorityOperations(uint64_t tick);
	void                      DeferOperation(Common::UniqueFunction<void>&& operation);
	void                      DeferPriorityOperation(Common::UniqueFunction<void>&& operation);
	[[nodiscard]] static bool InDeferredOperation() noexcept;
	// Hands later submissions to a dedicated queue thread, which submits them in tick order, so
	// vkQueueSubmit (and the queue lock that present also takes) leaves the recording thread.
	// Submit() then returns once the tick is allocated; host waits on a timeline value may precede
	// its signal operation. Enable before the first Submit().
	void EnableAsyncSubmit();
	[[nodiscard]] bool AsyncSubmit() const noexcept { return m_async_submit; }
	void EnableRecordingThread();
	[[nodiscard]] bool Threaded() const noexcept { return m_threaded; }
	vk::CommandBuffer DrainRecording();

	// Senaxx recorder protocol, with explicit cross-chunk ownership rather than a fixed
	// "retain two chunks" assumption. Every queued command owns all its stashed arrays.
	template<typename F> void Record(F&& operation) {
		using Function = std::decay_t<F>;
		struct Stored : RecordedCommand { Function function; };
		auto* stored = new (Allocate(sizeof(Stored), alignof(Stored))) Stored {
		    {}, std::forward<F>(operation)};
		stored->execute = +[](RecordedCommand* command, vk::CommandBuffer buffer) {
			static_cast<Stored*>(command)->function(buffer);
		};
		stored->destroy = +[](RecordedCommand* command) { static_cast<Stored*>(command)->~Stored(); };
		for (const auto& owner: m_stash_owners) {
			if (owner != m_chunk && std::find(m_chunk->owners.begin(), m_chunk->owners.end(), owner) == m_chunk->owners.end())
				m_chunk->owners.push_back(owner);
		}
		m_stash_owners.clear();
		if (m_chunk->last) m_chunk->last->next = stored;
		else m_chunk->first = stored;
		m_chunk->last = stored;
	}
	template<typename T> const T* Stash(const T* values, size_t count) {
		static_assert(std::is_trivially_copyable_v<T>);
		if (!count) return nullptr;
		EXIT_IF(values == nullptr || count > SIZE_MAX / sizeof(T));
		auto* copy = static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
		std::memcpy(copy, values, sizeof(T) * count);
		if (std::find(m_stash_owners.begin(), m_stash_owners.end(), m_chunk) == m_stash_owners.end())
			m_stash_owners.push_back(m_chunk);
		return copy;
	}

	[[nodiscard]] bool Active() const noexcept { return m_command.m_registers != nullptr; }
	void                           CheckActive() const;
	CommandBuffer&                 Current();
	[[nodiscard]] uint64_t         CurrentTick() const noexcept { return m_master.CurrentTick(); }
	[[nodiscard]] bool             IsFree(uint64_t tick);
	[[nodiscard]] MasterSemaphore& GetMasterSemaphore() noexcept { return m_master; }
	[[nodiscard]] RenderContext&   Context() const noexcept { return m_context; }
	[[nodiscard]] GraphicContext&  Graphics() const noexcept { return m_graphics; }

private:
	class CommandPool {
	public:
		CommandPool(GraphicContext& graphics, MasterSemaphore& master);
		~CommandPool();
		KYTY_CLASS_NO_COPY(CommandPool);

		vk::CommandBuffer Commit(uint64_t tick);

	private:
		static constexpr size_t GrowStep = 4;

		size_t Grow();

		GraphicContext&                m_graphics;
		MasterSemaphore&               m_master;
		vk::CommandPool                m_pool = nullptr;
		std::vector<vk::CommandBuffer> m_buffers;
		std::vector<uint64_t>          m_ticks;
		size_t                         m_hint = 0;
	};

	enum class OperationState { Open, Draining, Closed };

	struct PendingOperation {
		Common::UniqueFunction<void> callback;
		uint64_t                     tick = 0;
	};

	// A finished command buffer and everything its vkQueueSubmit needs, including the recording
	// state that a fatal submit report prints.
	struct SubmitJob {
		vk::CommandBuffer buffer = nullptr;
		SubmitInfo        submit;
		uint64_t          tick         = 0;
		uint32_t          debug_op     = 0;
		uint64_t          debug_submit = 0;
		uint32_t          debug_arg0   = 0;
		uint32_t          debug_arg1   = 0;
		uint32_t          debug_arg2   = 0;
		uint32_t          debug_arg3   = 0;
		uint64_t          debug_arg4   = 0;
	};

	void BeginNext();
	void QueueSubmit(SubmitJob& job);
	void SubmitThread(std::stop_token stop);
	void StopSubmitThread();
	void* Allocate(size_t size, size_t alignment);
	void DispatchChunk();
	void RecordingThread(std::stop_token stop);
	void StopRecordingThread();
	void PriorityOperationsThread(std::stop_token stop);
	void RunOperation(Common::UniqueFunction<void>&& operation);

	MasterSemaphore              m_master;
	RenderContext&               m_context;
	GraphicContext&              m_graphics;
	CommandPool                  m_command_pool;
	CommandBuffer                m_command;
	std::queue<PendingOperation> m_pending_operations;
	std::queue<PendingOperation> m_priority_operations;
	std::mutex                   m_operation_mutex;
	std::condition_variable      m_operation_available;
	std::jthread                 m_priority_thread;
	bool                         m_priority_active      = false;
	uint64_t                     m_priority_active_tick = 0;
	OperationState               m_operation_state      = OperationState::Open;
	bool                         m_async_submit         = false;
	std::deque<SubmitJob>        m_submit_jobs;
	std::mutex                   m_submit_mutex;
	std::condition_variable_any  m_submit_available;
	std::jthread                 m_submit_thread;
	struct RecordedCommand {
		RecordedCommand* next = nullptr;
		void (*execute)(RecordedCommand*, vk::CommandBuffer) = nullptr;
		void (*destroy)(RecordedCommand*) = nullptr;
	};
	struct Chunk {
		static constexpr size_t DefaultSize = 256 * 1024;
		explicit Chunk(size_t bytes = DefaultSize)
		    : storage((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t)) {}
		size_t Capacity() const { return storage.size() * sizeof(std::max_align_t); }
		std::vector<std::max_align_t> storage;
		size_t used = 0;
		RecordedCommand* first = nullptr;
		RecordedCommand* last = nullptr;
		std::vector<std::shared_ptr<Chunk>> owners;
	};
	bool m_threaded = false;
	std::jthread m_record_thread;
	std::shared_ptr<Chunk> m_chunk;
	std::vector<std::shared_ptr<Chunk>> m_stash_owners;
	std::deque<std::shared_ptr<Chunk>> m_record_queue;
	std::vector<std::shared_ptr<Chunk>> m_free_chunks;
	std::mutex m_record_mutex;
	std::condition_variable_any m_record_available;
	std::condition_variable m_record_executed;
	uint64_t m_chunks_dispatched = 0, m_chunks_executed = 0, m_record_drains = 0;
	vk::CommandBuffer m_worker_buffer = nullptr;
};

} // namespace Libs::Graphics

#include "graphics/host_gpu/renderer/commandRecorder.h"

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_COMMANDSCHEDULER_H_
