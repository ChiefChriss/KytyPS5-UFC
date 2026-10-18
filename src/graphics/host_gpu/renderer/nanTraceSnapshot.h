#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_NANTRACESNAPSHOT_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_NANTRACESNAPSHOT_H_

#include "graphics/host_gpu/renderer/cache/streamBuffer.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/shader/recompiler/NanTrace.h"

#include <cstring>
#include <optional>
#include <vector>

namespace Libs::Graphics {

// Diagnostics only. Never map/read the live shader-written buffer on the host. One bounded
// download slot is copied in queue order and read only after its timeline tick completes.
// Record/Consume run on the command lane; no extra submission or blocking wait is introduced.
class NanTraceSnapshot {
public:
	struct Completed {
		uint64_t frame;
		uint64_t tick;
		std::vector<uint32_t> words;
	};

	NanTraceSnapshot(GraphicContext& graphics, CommandScheduler& scheduler)
	    : m_scheduler(scheduler),
	      m_download(graphics, scheduler, MemoryUsage::Download, 0,
	                 vk::BufferUsageFlagBits::eTransferDst, ShaderRecompiler::NanTrace::BufferBytes) {}

	bool Record(const Buffer& producer, uint64_t frame) {
		if (m_pending) {
			return false; // Do not overwrite a snapshot that the host has not consumed.
		}
		// CopyFrom supplies shader-write -> transfer-read availability, transfer-read -> later
		// shader-write ordering on the producer, and transfer-write -> host-read visibility.
		m_download.CopyFrom(m_scheduler.Current(), producer, 0, 0,
		                    ShaderRecompiler::NanTrace::BufferBytes,
		                    vk::AccessFlagBits::eMemoryWrite, vk::AccessFlagBits::eHostRead,
		                    vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
		                    vk::AccessFlagBits::eHostRead);
		m_tick = m_scheduler.CurrentTick();
		m_frame = frame;
		m_pending = true;
		return true;
	}

	std::optional<Completed> Consume() {
		if (!m_pending || !m_scheduler.IsFree(m_tick)) {
			return std::nullopt; // Invalidation alone is NOT GPU-completion proof.
		}
		m_download.Invalidate(0, ShaderRecompiler::NanTrace::BufferBytes);
		Completed result {m_frame, m_tick,
		                  std::vector<uint32_t>(ShaderRecompiler::NanTrace::HeaderWords +
		                                        ShaderRecompiler::NanTrace::MaxSlots *
		                                            ShaderRecompiler::NanTrace::SlotWords)};
		std::memcpy(result.words.data(), m_download.Mapped().data(),
		            static_cast<size_t>(ShaderRecompiler::NanTrace::BufferBytes));
		m_pending = false;
		return result; // Owned words remain immutable even after the download slot is reused.
	}

private:
	CommandScheduler& m_scheduler;
	Buffer m_download;
	uint64_t m_tick = 0;
	uint64_t m_frame = 0;
	bool m_pending = false;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_NANTRACESNAPSHOT_H_
