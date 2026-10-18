#include "graphics/host_gpu/renderer/cache/dccClearEvaluator.h"

#include "common/assert.h"
#include "common/alignment.h"
#include "gpu_tiler_shaders/dcc_clear_check_spv.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"

#include <algorithm>
#include <iterator>

namespace Libs::Graphics {

namespace {

constexpr uint64_t PredicateBufferSize = 64 * 1024;
constexpr uint32_t WorkgroupSize       = 256;

struct PushConstants {
	uint32_t slice_words;
	uint32_t codes_low;
	uint32_t codes_high;
	uint32_t candidate_count;
};

} // namespace

DccClearEvaluator::DccClearEvaluator(GraphicContext& graphics, CommandScheduler& scheduler)
    : m_graphics(graphics),
      m_predicates(graphics, scheduler, MemoryUsage::DeviceLocal, 0,
                   AllFlags | vk::BufferUsageFlagBits::eConditionalRenderingEXT,
                   PredicateBufferSize) {
	SetVulkanObjectNameF(m_graphics.device, m_predicates.Handle(), "Kyty.DccClearPredicates");

	const vk::DescriptorSetLayoutBinding bindings[] {
	    {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	    {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	};
	vk::DescriptorSetLayoutCreateInfo layout_info {};
	layout_info.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	layout_info.bindingCount = static_cast<uint32_t>(std::size(bindings));
	layout_info.pBindings    = bindings;
	RequireVulkanSuccess(
	    m_graphics.device.createDescriptorSetLayout(&layout_info, nullptr, &m_descriptor_layout),
	    "create DCC clear descriptor layout");

	const vk::PushConstantRange push_range {vk::ShaderStageFlagBits::eCompute, 0,
	                                        sizeof(PushConstants)};
	vk::PipelineLayoutCreateInfo pipeline_layout_info {};
	pipeline_layout_info.setLayoutCount         = 1;
	pipeline_layout_info.pSetLayouts            = &m_descriptor_layout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges    = &push_range;
	RequireVulkanSuccess(m_graphics.device.createPipelineLayout(&pipeline_layout_info, nullptr,
	                                                            &m_pipeline_layout),
	                     "create DCC clear pipeline layout");

	const auto module = CompileSPV(DCC_CLEAR_CHECK_SPV, m_graphics.device);
	vk::PipelineShaderStageCreateInfo stage {};
	stage.stage  = vk::ShaderStageFlagBits::eCompute;
	stage.module = module;
	stage.pName  = "main";
	vk::ComputePipelineCreateInfo pipeline_info {};
	pipeline_info.stage  = stage;
	pipeline_info.layout = m_pipeline_layout;
	const auto result =
	    m_graphics.device.createComputePipelines(nullptr, 1, &pipeline_info, nullptr, &m_pipeline);
	m_graphics.device.destroyShaderModule(module, nullptr);
	RequireVulkanSuccess(result, "create DCC clear pipeline");
	SetVulkanObjectNameF(m_graphics.device, m_pipeline, "DCC Clear Code Check");
}

DccClearEvaluator::~DccClearEvaluator() {
	m_graphics.device.destroyPipeline(m_pipeline, nullptr);
	m_graphics.device.destroyPipelineLayout(m_pipeline_layout, nullptr);
	m_graphics.device.destroyDescriptorSetLayout(m_descriptor_layout, nullptr);
}

uint64_t DccClearEvaluator::Record(vk::CommandBuffer command, const Request& request) {
	if (request.slices == 0 || request.candidate_count == 0 ||
	    request.candidate_count > MaxCandidates || request.slice_size == 0 ||
	    request.slice_size % sizeof(uint32_t) != 0 ||
	    request.slice_size / sizeof(uint32_t) > UINT32_MAX) {
		return NoPredicates;
	}
	const uint64_t alignment = std::max<uint64_t>(
	    m_graphics.physical_device_properties.limits.minStorageBufferOffsetAlignment,
	    PredicateStride);
	const uint64_t bytes = uint64_t {request.slices} * PredicateStride;
	if (bytes > PredicateBufferSize) {
		return NoPredicates;
	}
	m_cursor = Common::AlignUp(m_cursor, alignment);
	if (m_cursor + bytes > PredicateBufferSize) {
		m_cursor = 0;
	}
	const uint64_t offset        = m_cursor;
	m_cursor                    += bytes;
	const uint64_t metadata_size = request.slice_size * request.slices;

	// Earlier GPU writes of the metadata and earlier conditional reads of the reused predicate
	// slots complete before the check (all prior commands on this queue).
	vk::BufferMemoryBarrier before[2] {};
	before[0].srcAccessMask       = vk::AccessFlagBits::eMemoryWrite;
	before[0].dstAccessMask       = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	before[0].buffer              = request.metadata;
	before[0].offset              = request.metadata_offset;
	before[0].size                = metadata_size;
	before[1]                     = before[0];
	before[1].srcAccessMask       = vk::AccessFlagBits::eConditionalRenderingReadEXT;
	before[1].dstAccessMask       = vk::AccessFlagBits::eShaderWrite;
	before[1].buffer              = m_predicates.Handle();
	before[1].offset              = offset;
	before[1].size                = bytes;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                        vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 2, before,
	                        0, nullptr);

	const vk::DescriptorBufferInfo infos[] {
	    {request.metadata, request.metadata_offset, metadata_size},
	    {m_predicates.Handle(), offset, bytes},
	};
	std::array<vk::WriteDescriptorSet, 2> writes {};
	for (uint32_t index = 0; index < writes.size(); ++index) {
		writes[index].dstBinding      = index;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType  = vk::DescriptorType::eStorageBuffer;
		writes[index].pBufferInfo     = &infos[index];
	}
	PushConstants constants {};
	constants.slice_words     = static_cast<uint32_t>(request.slice_size / sizeof(uint32_t));
	constants.candidate_count = request.candidate_count;
	for (uint32_t k = 0; k < request.candidate_count; k++) {
		if (k < 4) {
			constants.codes_low |= uint32_t {request.codes[k]} << (8u * k);
		} else {
			constants.codes_high = request.codes[k];
		}
	}
	command.bindPipeline(vk::PipelineBindPoint::eCompute, m_pipeline);
	command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, m_pipeline_layout, 0, writes);
	command.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
	                      sizeof(constants), &constants);
	static_assert(DccClearEvaluator::MaxCandidates <= WorkgroupSize);
	command.dispatch(request.slices, 1, 1);

	vk::BufferMemoryBarrier after[2] {before[0], before[1]};
	after[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
	after[0].dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
	after[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
	after[1].dstAccessMask = vk::AccessFlagBits::eConditionalRenderingReadEXT;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
	                        vk::PipelineStageFlagBits::eAllCommands, {}, 0, nullptr, 2, after, 0,
	                        nullptr);
	return offset;
}

} // namespace Libs::Graphics
