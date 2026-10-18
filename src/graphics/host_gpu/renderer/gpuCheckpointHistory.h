#ifndef EMULATOR_GRAPHICS_RENDERER_GPUCHECKPOINTHISTORY_H_
#define EMULATOR_GRAPHICS_RENDERER_GPUCHECKPOINTHISTORY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Libs::Graphics::GpuCrashDiagnostics {

// Owned recording-time identities. Stage is ShaderType, hash identifies guest code,
// program_id identifies the native specialization; neither is read from guest memory
// after a fault. Three vertex/tessellation stages and the optional pixel stage fit here.
struct GraphicsShaderIdentity {
	struct Stage {
		uint64_t hash = 0;
		uint64_t program_id = 0;
		uint32_t type = 0;
		uint32_t wave_size = 0;
		bool operator==(const Stage&) const = default;
	};
	std::array<Stage, 4> stages {};
	uint32_t count = 0;
	bool operator==(const GraphicsShaderIdentity&) const = default;
};

struct CheckpointInfo {
	uint32_t op = 0;
	uint64_t submit_id = 0;
	uint32_t args[4] = {};
	uint64_t arg4 = 0;
	uint64_t ps_addr = 0;
	uint64_t es_addr = 0;
	uint64_t gs_addr = 0;
	uint64_t shader_hash = 0;
	GraphicsShaderIdentity graphics;
};

// Reporting can race recording (including ring wrap and the legacy compute
// annotation). A sequence acquire alone cannot protect the non-atomic payload.
// Publish/read/annotation share a lock; readers leave with a fully owned copy.
template <std::size_t Capacity>
class CheckpointHistory {
public:
	static_assert(Capacity > 0);
	void Publish(uint64_t sequence, const CheckpointInfo& info) {
		std::lock_guard lock(m_mutex);
		m_records[sequence % Capacity] = {sequence, info};
	}
	[[nodiscard]] std::optional<CheckpointInfo> Read(uint64_t sequence) const {
		std::lock_guard lock(m_mutex);
		const auto& record = m_records[sequence % Capacity];
		if (sequence == 0 || record.sequence != sequence) return std::nullopt;
		return record.info;
	}
	bool AnnotateCompute(uint64_t sequence, uint64_t hash) {
		std::lock_guard lock(m_mutex);
		auto& record = m_records[sequence % Capacity];
		if (sequence == 0 || record.sequence != sequence || record.info.graphics.count != 0) return false;
		record.info.shader_hash = hash;
		return true;
	}
private:
	struct Record {
		uint64_t sequence = 0;
		CheckpointInfo info;
	};
	mutable std::mutex m_mutex;
	std::array<Record, Capacity> m_records {};
};

} // namespace Libs::Graphics::GpuCrashDiagnostics
#endif
