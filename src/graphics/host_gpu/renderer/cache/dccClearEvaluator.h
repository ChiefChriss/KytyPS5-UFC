#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_DCCCLEAREVALUATOR_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_DCCCLEAREVALUATOR_H_

#include "common/common.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <cstdint>

namespace Libs::Graphics {

struct GraphicContext;
class CommandScheduler;

// Evaluates single-mip color metadata clear codes on the GPU timeline. One workgroup per slice
// checks whether every metadata byte equals the first one and whether that byte is one of the
// caller's decodable clear codes. It writes one 32-bit predicate per candidate and, on a match,
// expands the slice to 0xff exactly like the CPU materialization path. The caller then clears
// the target under VK_EXT_conditional_rendering, so GPU-owned metadata needs no CPU readback.
class DccClearEvaluator final {
public:
	static constexpr uint32_t MaxCandidates   = 5;
	static constexpr uint32_t PredicateStride = 8 * sizeof(uint32_t);
	static constexpr uint64_t NoPredicates    = UINT64_MAX;

	struct Request {
		vk::Buffer                         metadata        = nullptr;
		uint64_t                           metadata_offset = 0;
		uint64_t                           slice_size      = 0;
		uint32_t                           slices          = 0;
		std::array<uint8_t, MaxCandidates> codes {};
		uint32_t                           candidate_count = 0;
	};

	DccClearEvaluator(GraphicContext& graphics, CommandScheduler& scheduler);
	~DccClearEvaluator();
	KYTY_CLASS_NO_COPY(DccClearEvaluator);

	// Records the evaluation outside rendering. Returns the byte offset of the first slice's
	// predicates in PredicateBuffer() (slice s, candidate k: offset + s * PredicateStride + 4 * k),
	// or NoPredicates when the request does not fit.
	[[nodiscard]] uint64_t   Record(vk::CommandBuffer command, const Request& request);
	[[nodiscard]] vk::Buffer PredicateBuffer() const { return m_predicates.Handle(); }

private:
	GraphicContext&         m_graphics;
	Buffer                  m_predicates;
	uint64_t                m_cursor            = 0;
	vk::DescriptorSetLayout m_descriptor_layout = nullptr;
	vk::PipelineLayout      m_pipeline_layout   = nullptr;
	vk::Pipeline            m_pipeline          = nullptr;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_DCCCLEAREVALUATOR_H_
