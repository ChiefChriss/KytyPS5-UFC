#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_NANTRACE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_NANTRACE_H_

#include <cstdint>
#include <span>
#include <vector>

#include "graphics/shader/recompiler/ir/ResourceSnapshot.h"

// Opt-in NaN/Inf tracer (KYTY_UFC_NAN_TRACE=1), diagnostics only.
//
// Every translated shader checks the floating-point values it is about to write (pixel colour
// exports, vertex position exports, image stores). The first time a given write site produces a
// NaN or an infinity, the shader atomically stamps that site's slot with a counter value.
// The renderer copies completed queue-ordered snapshots; the host never reads the live buffer.
// Stamps are atomic counter order, NOT causal draw/event order. Frame/tick identify the snapshot
// boundary, NOT the exact event producing the value. A push token identifies the recording
// event; the CAS winner also saves that token and the unmodified output words. Publication
// is complete only at a barriered snapshot boundary, never at the stamp alone.
namespace Libs::Graphics::ShaderRecompiler::NanTrace {

enum class Site : uint32_t {
	ColorExport    = 0,
	PositionExport = 1,
	ImageStore     = 2,
};

inline constexpr uint32_t HeaderWords = 4; // word 0: order counter
inline constexpr uint32_t MaxSlots    = 65536;
inline constexpr uint32_t SlotWords   = 6; // stamp, event token, raw float4
inline constexpr uint32_t EventPushDword = 31; // diagnostic-only reserved tail word
inline constexpr uint32_t MaxEvents   = 262144; // bounded host recording history
inline constexpr uint32_t MaxInputEvents = 4096;
inline constexpr uint32_t MaxInputBytes = 16384; // per selected binding commit; no partial records
inline constexpr uint64_t BufferBytes = static_cast<uint64_t>(HeaderWords + MaxSlots * SlotWords) * 4u;
inline constexpr uint32_t NoSlot      = UINT32_MAX;

struct EventInfo {
	uint64_t frame = 0;
	uint64_t tick = 0;
	uint64_t submit = 0;
	uint64_t address = 0;
	uint32_t op = 0;
	uint32_t args[4] {};
};

// Owned CPU materialization inputs, NOT GPU buffer/image contents. Never dereference guest
// addresses at Poll time. Selection is deliberately limited to the existing VS position leads.
struct StageInputs {
	struct BufferBinding {
		uint32_t resource = 0;
		uint64_t address = 0;
		uint64_t size = 0;
		uint64_t native_offset = 0;
		uint64_t native_range = 0;
	};
	struct ImageBinding {
		uint64_t address = 0;
		uint64_t size = 0;
		uint32_t guest_format = 0;
		uint32_t native_format = 0;
		uint32_t base_level = 0;
		uint32_t level_count = 0;
		uint32_t base_layer = 0;
		uint32_t layer_count = 0;
	};
	uint64_t shader_hash = 0;
	uint32_t stage = 0;
	uint32_t wave_size = 0;
	uint32_t user_data_base = 0;
	bool address_writes = false;
	bool uses_bda = false;
	bool shader_data_in_push = false;
	uint32_t shader_data_start = 0;
	IR::ResourceSnapshot resources;
	std::vector<uint32_t> shader_data;
	std::vector<uint32_t> user_data_registers;
	std::vector<BufferBinding> buffers;
	std::vector<ImageBinding> images;
	std::array<uint32_t, 32> push_words {};
};

[[nodiscard]] bool SelectInputShader(uint64_t shader_hash);
// Accounting includes all vector payloads; oversized events are rejected, never truncated.
[[nodiscard]] uint64_t InputBytes(std::span<const StageInputs> inputs);

// Zero means no event. IDs never wrap/reuse; expired history is explicitly unavailable.
[[nodiscard]] uint32_t RecordEvent(const EventInfo& info, std::vector<StageInputs> inputs = {});
[[nodiscard]] bool LookupEvent(uint32_t token, EventInfo& info);
[[nodiscard]] bool LookupInputs(uint32_t token, std::vector<StageInputs>& inputs);

// KYTY_UFC_NAN_TRACE=1 asks for the tracer; it is active once the renderer has a buffer.
[[nodiscard]] bool Requested();
void               Activate(uint64_t device_address);
void               Deactivate();
[[nodiscard]] bool Active();
[[nodiscard]] uint64_t DeviceAddress();

// Called while translating; the same (shader, stage, site, index, kind) always gets the same slot.
[[nodiscard]] uint32_t RegisterSlot(uint64_t shader_hash, const char* stage, Site site,
                                    uint32_t index, bool infinity);

// Only completed, host-visible, immutable snapshots may be supplied by the renderer.
void Poll(uint64_t frame, uint64_t tick, std::span<const uint32_t> words);

} // namespace Libs::Graphics::ShaderRecompiler::NanTrace

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_NANTRACE_H_ */
