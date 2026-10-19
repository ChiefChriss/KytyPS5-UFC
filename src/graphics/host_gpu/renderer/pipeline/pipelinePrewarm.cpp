#include "graphics/host_gpu/renderer/pipeline/pipelinePrewarm.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/host_gpu/graphicContext.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <xxhash.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Libs::Graphics {

namespace {

constexpr char     FileMagic[8]     = {'K', 'Y', 'T', 'Y', 'P', 'W', '0', '1'};
constexpr uint8_t  RecipeFormat     = 2;
constexpr uint64_t MaxFileBytes     = 1ull << 30u;
constexpr size_t   MaxRecipes       = 200000;
constexpr uint32_t MaxRecordBytes   = 64u << 20u;
constexpr uint32_t MaxVectorEntries = 4096;

uint64_t SteadyNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
	                                 std::chrono::steady_clock::now().time_since_epoch())
	                                 .count());
}

template <typename... Args>
void PrewarmLog(fmt::format_string<Args...> format, Args&&... args) {
	auto message = fmt::format(format, std::forward<Args>(args)...);
	message += '\n';
	Log::WriteToConsoleAndLog(message);
}

std::string ModuleKey(const SpirvModuleId& id) {
	std::string key(sizeof(id.lo) + sizeof(id.hi), '\0');
	std::memcpy(key.data(), &id.lo, sizeof(id.lo));
	std::memcpy(key.data() + sizeof(id.lo), &id.hi, sizeof(id.hi));
	return key;
}

std::string BytesKey(std::span<const uint8_t> bytes) {
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

class Writer {
public:
	void U8(uint8_t value) { m_bytes.push_back(value); }
	void Bool(bool value) { U8(value ? 1u : 0u); }
	void U32(uint32_t value) {
		for (uint32_t shift = 0; shift < 32; shift += 8) {
			m_bytes.push_back(static_cast<uint8_t>(value >> shift));
		}
	}
	void U64(uint64_t value) {
		U32(static_cast<uint32_t>(value));
		U32(static_cast<uint32_t>(value >> 32u));
	}
	void F32(float value) { U32(std::bit_cast<uint32_t>(value)); }
	template <typename Enum>
	void Enum32(Enum value) {
		U32(static_cast<uint32_t>(value));
	}
	template <typename Flags>
	void Flags32(Flags value) {
		U32(static_cast<uint32_t>(value));
	}
	std::vector<uint8_t> Take() { return std::move(m_bytes); }

private:
	std::vector<uint8_t> m_bytes;
};

class Reader {
public:
	explicit Reader(std::span<const uint8_t> bytes): m_bytes(bytes) {}
	uint8_t U8() {
		if (m_offset + 1 > m_bytes.size()) {
			m_ok = false;
			return 0;
		}
		return m_bytes[m_offset++];
	}
	bool     Bool() { return U8() != 0; }
	uint32_t U32() {
		if (m_offset + 4 > m_bytes.size()) {
			m_ok = false;
			return 0;
		}
		uint32_t value = 0;
		for (uint32_t i = 0; i < 4; i++) {
			value |= static_cast<uint32_t>(m_bytes[m_offset + i]) << (8u * i);
		}
		m_offset += 4;
		return value;
	}
	uint64_t U64() {
		const uint64_t low  = U32();
		const uint64_t high = U32();
		return low | (high << 32u);
	}
	float F32() { return std::bit_cast<float>(U32()); }
	template <typename Enum>
	Enum Enum32() {
		return static_cast<Enum>(U32());
	}
	template <typename Flags>
	Flags Flags32() {
		return Flags(static_cast<typename Flags::MaskType>(U32()));
	}
	uint32_t Count() {
		const auto count = U32();
		if (count > MaxVectorEntries) {
			m_ok = false;
			return 0;
		}
		return count;
	}
	[[nodiscard]] bool Done() const { return m_ok && m_offset == m_bytes.size(); }
	[[nodiscard]] bool Ok() const { return m_ok; }

private:
	std::span<const uint8_t> m_bytes;
	size_t                   m_offset = 0;
	bool                     m_ok     = true;
};

void SetBackgroundPriority() {
#if defined(_WIN32)
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

} // namespace

SpirvModuleId SpirvModuleId::Of(std::span<const uint32_t> words) {
	const auto hash = XXH3_128bits(words.data(), words.size_bytes());
	SpirvModuleId id {.lo = hash.low64, .hi = hash.high64};
	if (!id.Valid()) {
		id.lo = 1;
	}
	return id;
}

std::vector<uint8_t> PipelineRecipe::Serialize() const {
	Writer w;
	w.U8(RecipeFormat);
	w.U8(static_cast<uint8_t>(kind));
	w.Flags32(flags);
	w.Bool(uses_bindless);
	w.U32(static_cast<uint32_t>(stages.size()));
	for (const auto& stage: stages) {
		w.Flags32(stage.stage);
		w.U64(stage.module.lo);
		w.U64(stage.module.hi);
		w.U32(stage.required_subgroup_size);
	}
	w.U32(static_cast<uint32_t>(descriptors.size()));
	for (const auto& binding: descriptors) {
		EXIT_IF(binding.pImmutableSamplers != nullptr);
		w.U32(binding.binding);
		w.Enum32(binding.descriptorType);
		w.U32(binding.descriptorCount);
		w.Flags32(binding.stageFlags);
	}
	w.Bool(push_descriptors);
	w.Flags32(push_constant_stages);
	w.U32(push_constant_size);
	if (kind == Kind::Compute) {
		return w.Take();
	}
	w.Bool(mesh);
	w.U32(static_cast<uint32_t>(vertex_bindings.size()));
	for (const auto& binding: vertex_bindings) {
		w.U32(binding.binding);
		w.U32(binding.stride);
		w.Enum32(binding.inputRate);
	}
	w.U32(static_cast<uint32_t>(vertex_attributes.size()));
	for (const auto& attribute: vertex_attributes) {
		w.U32(attribute.location);
		w.U32(attribute.binding);
		w.Enum32(attribute.format);
		w.U32(attribute.offset);
	}
	w.Enum32(topology);
	w.Bool(primitive_restart);
	w.Bool(tessellation_state);
	w.U32(patch_control_points);
	w.Bool(negative_one_to_one);
	w.Bool(depth_clamp);
	w.Bool(depth_clip_struct);
	w.Bool(depth_clip_enable);
	w.Bool(provoking_struct);
	w.Enum32(provoking_mode);
	w.Flags32(cull_mode);
	w.Enum32(front_face);
	w.Enum32(polygon_mode);
	w.Flags32(samples);
	w.Bool(sample_shading);
	w.Bool(depth_stencil_state);
	w.Bool(depth_bounds_test);
	w.F32(min_depth_bounds);
	w.F32(max_depth_bounds);
	w.U32(static_cast<uint32_t>(blend_attachments.size()));
	for (const auto& blend: blend_attachments) {
		w.U32(blend.blendEnable);
		w.Enum32(blend.srcColorBlendFactor);
		w.Enum32(blend.dstColorBlendFactor);
		w.Enum32(blend.colorBlendOp);
		w.Enum32(blend.srcAlphaBlendFactor);
		w.Enum32(blend.dstAlphaBlendFactor);
		w.Enum32(blend.alphaBlendOp);
		w.Flags32(blend.colorWriteMask);
	}
	w.Bool(color_write_struct);
	w.U32(color_count);
	for (const auto format: color_formats) {
		w.Enum32(format);
	}
	w.Enum32(depth_format);
	w.Enum32(stencil_format);
	w.U32(static_cast<uint32_t>(dynamic_states.size()));
	for (const auto state: dynamic_states) {
		w.Enum32(state);
	}
	return w.Take();
}

bool PipelineRecipe::Deserialize(std::span<const uint8_t> bytes, PipelineRecipe& recipe) {
	Reader r(bytes);
	recipe = {};
	if (r.U8() != RecipeFormat) {
		return false;
	}
	const auto kind = r.U8();
	if (kind != static_cast<uint8_t>(Kind::Graphics) && kind != static_cast<uint8_t>(Kind::Compute)) {
		return false;
	}
	recipe.kind          = static_cast<Kind>(kind);
	recipe.flags         = r.Flags32<vk::PipelineCreateFlags>();
	recipe.uses_bindless = r.Bool();
	recipe.stages.resize(r.Count());
	for (auto& stage: recipe.stages) {
		stage.stage                  = static_cast<vk::ShaderStageFlagBits>(r.U32());
		stage.module.lo              = r.U64();
		stage.module.hi              = r.U64();
		stage.required_subgroup_size = r.U32();
	}
	recipe.descriptors.resize(r.Count());
	for (auto& binding: recipe.descriptors) {
		binding.binding            = r.U32();
		binding.descriptorType     = r.Enum32<vk::DescriptorType>();
		binding.descriptorCount    = r.U32();
		binding.stageFlags         = r.Flags32<vk::ShaderStageFlags>();
		binding.pImmutableSamplers = nullptr;
	}
	recipe.push_descriptors     = r.Bool();
	recipe.push_constant_stages = r.Flags32<vk::ShaderStageFlags>();
	recipe.push_constant_size   = r.U32();
	if (recipe.kind == Kind::Compute) {
		return r.Done() && recipe.stages.size() == 1;
	}
	recipe.mesh = r.Bool();
	recipe.vertex_bindings.resize(r.Count());
	for (auto& binding: recipe.vertex_bindings) {
		binding.binding   = r.U32();
		binding.stride    = r.U32();
		binding.inputRate = r.Enum32<vk::VertexInputRate>();
	}
	recipe.vertex_attributes.resize(r.Count());
	for (auto& attribute: recipe.vertex_attributes) {
		attribute.location = r.U32();
		attribute.binding  = r.U32();
		attribute.format   = r.Enum32<vk::Format>();
		attribute.offset   = r.U32();
	}
	recipe.topology             = r.Enum32<vk::PrimitiveTopology>();
	recipe.primitive_restart    = r.Bool();
	recipe.tessellation_state   = r.Bool();
	recipe.patch_control_points = r.U32();
	recipe.negative_one_to_one  = r.Bool();
	recipe.depth_clamp          = r.Bool();
	recipe.depth_clip_struct    = r.Bool();
	recipe.depth_clip_enable    = r.Bool();
	recipe.provoking_struct     = r.Bool();
	recipe.provoking_mode       = r.Enum32<vk::ProvokingVertexModeEXT>();
	recipe.cull_mode            = r.Flags32<vk::CullModeFlags>();
	recipe.front_face           = r.Enum32<vk::FrontFace>();
	recipe.polygon_mode         = r.Enum32<vk::PolygonMode>();
	recipe.samples              = static_cast<vk::SampleCountFlagBits>(r.U32());
	recipe.sample_shading       = r.Bool();
	recipe.depth_stencil_state  = r.Bool();
	recipe.depth_bounds_test    = r.Bool();
	recipe.min_depth_bounds     = r.F32();
	recipe.max_depth_bounds     = r.F32();
	recipe.blend_attachments.resize(r.Count());
	for (auto& blend: recipe.blend_attachments) {
		blend.blendEnable         = r.U32();
		blend.srcColorBlendFactor = r.Enum32<vk::BlendFactor>();
		blend.dstColorBlendFactor = r.Enum32<vk::BlendFactor>();
		blend.colorBlendOp        = r.Enum32<vk::BlendOp>();
		blend.srcAlphaBlendFactor = r.Enum32<vk::BlendFactor>();
		blend.dstAlphaBlendFactor = r.Enum32<vk::BlendFactor>();
		blend.alphaBlendOp        = r.Enum32<vk::BlendOp>();
		blend.colorWriteMask      = r.Flags32<vk::ColorComponentFlags>();
	}
	recipe.color_write_struct = r.Bool();
	recipe.color_count        = r.U32();
	for (auto& format: recipe.color_formats) {
		format = r.Enum32<vk::Format>();
	}
	recipe.depth_format   = r.Enum32<vk::Format>();
	recipe.stencil_format = r.Enum32<vk::Format>();
	recipe.dynamic_states.resize(r.Count());
	for (auto& state: recipe.dynamic_states) {
		state = r.Enum32<vk::DynamicState>();
	}
	return r.Done() && !recipe.stages.empty() && recipe.color_count <= RENDER_COLOR_ATTACHMENTS_MAX &&
	       recipe.blend_attachments.size() == recipe.color_count;
}

bool CreatePipelineFromRecipe(GraphicContext& graphics, const PipelineRecipe& recipe,
                              std::span<const vk::ShaderModule> modules,
                              vk::PipelineCache driver_cache, PipelineCache::Pipeline& out,
                              uint64_t* create_ns) {
	if (modules.size() != recipe.stages.size()) {
		return false;
	}
	PipelineCache::Pipeline pipeline;
	auto                    fail = [&] {
		if (pipeline.pipeline_layout != nullptr) {
			graphics.device.destroyPipelineLayout(pipeline.pipeline_layout, nullptr);
		}
		if (pipeline.descriptor_set_layout != nullptr) {
			graphics.device.destroyDescriptorSetLayout(pipeline.descriptor_set_layout, nullptr);
		}
		return false;
	};

	pipeline.uses_push_descriptors = recipe.push_descriptors;
	vk::DescriptorSetLayoutCreateInfo set_layout {};
	set_layout.flags        = recipe.push_descriptors
	                              ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
	                              : vk::DescriptorSetLayoutCreateFlags {};
	set_layout.bindingCount = static_cast<uint32_t>(recipe.descriptors.size());
	set_layout.pBindings    = recipe.descriptors.data();
	if (graphics.device.createDescriptorSetLayout(&set_layout, nullptr,
	                                              &pipeline.descriptor_set_layout) !=
	    vk::Result::eSuccess) {
		return fail();
	}

	const vk::PushConstantRange push_constants {recipe.push_constant_stages, 0,
	                                            recipe.push_constant_size};
	std::array<vk::DescriptorSetLayout, 2> set_layouts {pipeline.descriptor_set_layout, nullptr};
	pipeline.uses_bindless = recipe.uses_bindless;
	if (recipe.uses_bindless) {
		if (graphics.bindless_layout == nullptr) {
			return fail();
		}
		set_layouts[1] = graphics.bindless_layout;
	}
	vk::PipelineLayoutCreateInfo layout_info {};
	layout_info.setLayoutCount         = recipe.uses_bindless ? 2u : 1u;
	layout_info.pSetLayouts            = set_layouts.data();
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges    = &push_constants;
	if (graphics.device.createPipelineLayout(&layout_info, nullptr, &pipeline.pipeline_layout) !=
	    vk::Result::eSuccess) {
		return fail();
	}

	vk::Result result     = vk::Result::eErrorUnknown;
	uint64_t   begin      = 0;
	uint64_t   elapsed_ns = 0;
	if (recipe.kind == PipelineRecipe::Kind::Compute) {
		vk::PipelineShaderStageCreateInfo                     stage {};
		vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup {};
		stage.stage  = vk::ShaderStageFlagBits::eCompute;
		stage.module = modules[0];
		stage.pName  = "main";
		if (recipe.stages[0].required_subgroup_size != 0) {
			subgroup.requiredSubgroupSize = recipe.stages[0].required_subgroup_size;
			stage.pNext                   = &subgroup;
		}
		vk::ComputePipelineCreateInfo info {};
		info.flags             = recipe.flags;
		info.stage             = stage;
		info.layout            = pipeline.pipeline_layout;
		info.basePipelineIndex = -1;
		begin                  = SteadyNs();
		result = graphics.device.createComputePipelines(driver_cache, 1, &info, nullptr,
		                                                &pipeline.pipeline);
		elapsed_ns = SteadyNs() - begin;
	} else {
		std::array<vk::PipelineShaderStageCreateInfo, 8>                     stages {};
		std::array<vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo, 8> subgroups {};
		if (recipe.stages.size() > stages.size()) {
			return fail();
		}
		for (size_t i = 0; i < recipe.stages.size(); i++) {
			stages[i] = {.stage = recipe.stages[i].stage, .module = modules[i], .pName = "main"};
			if (recipe.stages[i].required_subgroup_size != 0) {
				subgroups[i].requiredSubgroupSize = recipe.stages[i].required_subgroup_size;
				stages[i].pNext                   = &subgroups[i];
			}
		}

		vk::PipelineVertexInputStateCreateInfo vertex_input {};
		vertex_input.vertexBindingDescriptionCount =
		    static_cast<uint32_t>(recipe.vertex_bindings.size());
		vertex_input.pVertexBindingDescriptions = recipe.vertex_bindings.data();
		vertex_input.vertexAttributeDescriptionCount =
		    static_cast<uint32_t>(recipe.vertex_attributes.size());
		vertex_input.pVertexAttributeDescriptions = recipe.vertex_attributes.data();

		vk::PipelineInputAssemblyStateCreateInfo input_assembly {};
		input_assembly.topology               = recipe.topology;
		input_assembly.primitiveRestartEnable = recipe.primitive_restart ? VK_TRUE : VK_FALSE;

		vk::PipelineViewportDepthClipControlCreateInfoEXT depth_clip_control {};
		depth_clip_control.negativeOneToOne = recipe.negative_one_to_one ? VK_TRUE : VK_FALSE;
		vk::PipelineViewportStateCreateInfo viewport_state {};
		viewport_state.pNext = &depth_clip_control;

		vk::PipelineRasterizationDepthClipStateCreateInfoEXT clip_ext {};
		clip_ext.depthClipEnable = recipe.depth_clip_enable ? VK_TRUE : VK_FALSE;
		vk::PipelineRasterizationStateCreateInfo rasterizer {};
		rasterizer.depthClampEnable = recipe.depth_clamp ? VK_TRUE : VK_FALSE;
		if (recipe.depth_clip_struct) {
			rasterizer.pNext = &clip_ext;
		}
		vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT provoking_vertex {};
		if (recipe.provoking_struct) {
			provoking_vertex.provokingVertexMode = recipe.provoking_mode;
			provoking_vertex.pNext               = rasterizer.pNext;
			rasterizer.pNext                     = &provoking_vertex;
		}
		rasterizer.cullMode    = recipe.cull_mode;
		rasterizer.frontFace   = recipe.front_face;
		rasterizer.polygonMode = recipe.polygon_mode;
		rasterizer.lineWidth   = 1.0f;

		vk::PipelineMultisampleStateCreateInfo multisampling {};
		multisampling.sampleShadingEnable  = recipe.sample_shading ? VK_TRUE : VK_FALSE;
		multisampling.rasterizationSamples = recipe.samples;
		multisampling.minSampleShading     = 1.0f;

		std::array<vk::Bool32, RENDER_COLOR_ATTACHMENTS_MAX> color_write_enable {};
		for (uint32_t i = 0; i < recipe.color_count; i++) {
			color_write_enable[i] = VK_TRUE;
		}
		vk::PipelineColorWriteCreateInfoEXT color_write {};
		color_write.attachmentCount    = recipe.color_count;
		color_write.pColorWriteEnables = color_write_enable.data();
		vk::PipelineColorBlendStateCreateInfo color_blending {};
		if (recipe.color_write_struct) {
			color_blending.pNext = &color_write;
		}
		color_blending.logicOp         = vk::LogicOp::eCopy;
		color_blending.attachmentCount = recipe.color_count;
		color_blending.pAttachments    = recipe.blend_attachments.data();

		vk::PipelineDepthStencilStateCreateInfo depth_stencil {};
		depth_stencil.depthBoundsTestEnable = recipe.depth_bounds_test ? VK_TRUE : VK_FALSE;
		depth_stencil.minDepthBounds        = recipe.min_depth_bounds;
		depth_stencil.maxDepthBounds        = recipe.max_depth_bounds;

		vk::PipelineDynamicStateCreateInfo dynamic_state {};
		dynamic_state.dynamicStateCount = static_cast<uint32_t>(recipe.dynamic_states.size());
		dynamic_state.pDynamicStates    = recipe.dynamic_states.data();

		vk::PipelineRenderingCreateInfo rendering {};
		rendering.colorAttachmentCount    = recipe.color_count;
		rendering.pColorAttachmentFormats = recipe.color_formats.data();
		rendering.depthAttachmentFormat   = recipe.depth_format;
		rendering.stencilAttachmentFormat = recipe.stencil_format;

		vk::PipelineTessellationStateCreateInfo tessellation {};
		tessellation.patchControlPoints = recipe.patch_control_points;

		vk::GraphicsPipelineCreateInfo info {};
		info.flags               = recipe.flags;
		info.pNext               = &rendering;
		info.stageCount          = static_cast<uint32_t>(recipe.stages.size());
		info.pStages             = stages.data();
		info.pVertexInputState   = recipe.mesh ? nullptr : &vertex_input;
		info.pInputAssemblyState = recipe.mesh ? nullptr : &input_assembly;
		info.pTessellationState  = recipe.tessellation_state ? &tessellation : nullptr;
		info.pViewportState      = &viewport_state;
		info.pRasterizationState = &rasterizer;
		info.pMultisampleState   = &multisampling;
		info.pDepthStencilState  = recipe.depth_stencil_state ? &depth_stencil : nullptr;
		info.pColorBlendState    = &color_blending;
		info.pDynamicState       = &dynamic_state;
		info.layout              = pipeline.pipeline_layout;
		info.basePipelineIndex   = -1;
		begin                    = SteadyNs();
		result = graphics.device.createGraphicsPipelines(driver_cache, 1, &info, nullptr,
		                                                 &pipeline.pipeline);
		elapsed_ns = SteadyNs() - begin;
	}
	if (create_ns != nullptr) {
		*create_ns = elapsed_ns;
	}
	if (result != vk::Result::eSuccess || pipeline.pipeline == nullptr) {
		pipeline.pipeline = nullptr;
		return fail();
	}
	out = pipeline;
	return true;
}

PipelinePrewarm::Mode PipelinePrewarm::ConfiguredMode() {
	const char* value = std::getenv("KYTY_UFC_PIPELINE_PREWARM");
	if (value == nullptr || std::strcmp(value, "1") == 0) {
		return Mode::RecordAndReplay;
	}
	if (std::strcmp(value, "record") == 0) {
		return Mode::Record;
	}
	return Mode::Off;
}

PipelinePrewarm::PipelinePrewarm(GraphicContext& graphics, vk::PipelineCache driver_cache,
                                 const std::string& title_id, Mode mode)
    : m_graphics(graphics), m_driver_cache(driver_cache), m_mode(mode),
      m_path(std::filesystem::path("_PipelineCache") / (title_id + ".prewarm")),
      m_start_ns(SteadyNs()) {
	if (m_mode == Mode::Off || title_id.empty()) {
		m_mode   = Mode::Off;
		m_loaded = true;
		return;
	}
	m_loader = std::jthread([this](std::stop_token stop) {
		KYTY_PROFILER_THREAD("Thread_PipelinePrewarmLoad");
		Load();
		if (m_mode != Mode::RecordAndReplay || stop.stop_requested() || m_entries.empty()) {
			return;
		}
		uint32_t threads = std::clamp(std::thread::hardware_concurrency() / 4u, 1u, 4u);
		if (const char* value = std::getenv("KYTY_UFC_PIPELINE_PREWARM_THREADS"); value != nullptr) {
			threads = static_cast<uint32_t>(std::clamp(std::atoi(value), 1, 16));
		}
		PrewarmLog("Pipeline prewarm: building {} recipes on {} threads", m_entries.size(), threads);
		m_active_builders.store(threads, std::memory_order_relaxed);
		for (uint32_t i = 0; i < threads; i++) {
			m_builders.emplace_back([this](std::stop_token builder_stop) {
				KYTY_PROFILER_THREAD("Thread_PipelinePrewarm");
				SetBackgroundPriority();
				BuildEntries(builder_stop);
				if (m_active_builders.fetch_sub(1, std::memory_order_acq_rel) != 1) {
					return;
				}
				// Pipelines no longer need their modules once created.
				std::scoped_lock lock(m_module_mutex);
				for (const auto& [key, module]: m_modules) {
					(void)key;
					m_graphics.device.destroyShaderModule(module, nullptr);
				}
				m_modules.clear();
				m_module_words.clear();
				LogSummary();
			});
		}
	});
}

void PipelinePrewarm::Stop() {
	if (m_loader.joinable()) {
		m_loader.request_stop();
		m_loader.join();
	}
	for (auto& builder: m_builders) {
		builder.request_stop();
	}
	m_builders.clear();
}

PipelinePrewarm::~PipelinePrewarm() {
	Stop();
	for (auto& entry: m_entries) {
		if (entry.state == State::Ready || entry.state == State::Taken) {
			DestroyPipeline(entry.pipeline);
		}
	}
	for (const auto& [key, module]: m_modules) {
		(void)key;
		m_graphics.device.destroyShaderModule(module, nullptr);
	}
	if (m_file != nullptr) {
		std::fclose(m_file);
	}
}

std::string PipelinePrewarm::Signature() const {
	const auto& properties = m_graphics.GetPhysicalDeviceProperties();
	std::string uuid;
	for (const auto byte: properties.pipelineCacheUUID) {
		uuid += fmt::format("{:02x}", static_cast<unsigned>(byte));
	}
	return fmt::format("{:08x}:{:08x}:{:08x}:{}:mesh{}:provoke{}:feedback{}:subgroup{}-{}-{}:push{}",
	                   properties.vendorID, properties.deviceID, properties.driverVersion, uuid,
	                   m_graphics.mesh_shader_enabled ? 1 : 0,
	                   m_graphics.provoking_vertex_last_enabled ? 1 : 0,
	                   m_graphics.attachment_feedback_loop_enabled ? 1 : 0,
	                   m_graphics.compute_subgroup_size_control_enabled ? 1 : 0,
	                   m_graphics.min_subgroup_size, m_graphics.max_subgroup_size,
	                   m_graphics.max_push_descriptors);
}

void PipelinePrewarm::Load() {
	const auto  signature = Signature();
	std::string header(FileMagic, sizeof(FileMagic));
	{
		Writer w;
		w.U32(static_cast<uint32_t>(signature.size()));
		const auto length = w.Take();
		header.append(reinterpret_cast<const char*>(length.data()), length.size());
		header += signature;
	}

	std::vector<uint8_t> contents;
	std::error_code      error;
	const auto           file_size = std::filesystem::file_size(m_path, error);
	const char*          reason    = nullptr;
	if (!error && file_size > MaxFileBytes) {
		reason = "file too large";
	} else if (!error && file_size > 0) {
		if (std::FILE* in = std::fopen(m_path.string().c_str(), "rb"); in != nullptr) {
			contents.resize(file_size);
			if (std::fread(contents.data(), 1, contents.size(), in) != contents.size()) {
				contents.clear();
				reason = "read failed";
			}
			std::fclose(in);
		}
	}
	size_t good_end    = 0;
	bool   header_good = contents.size() >= header.size() &&
	                   std::memcmp(contents.data(), header.data(), header.size()) == 0;
	if (!contents.empty() && !header_good && reason == nullptr) {
		reason = "device, driver or format changed";
	}

	std::vector<Entry>                                     entries;
	std::unordered_map<std::string, size_t>                index;
	std::unordered_set<std::string>                        recorded;
	std::unordered_set<std::string>                        known_modules;
	std::unordered_map<std::string, std::vector<uint32_t>> module_words;
	if (header_good) {
		size_t offset = header.size();
		good_end      = offset;
		while (offset + 13 <= contents.size()) {
			Reader     prefix(std::span(contents).subspan(offset, 13));
			const auto type = static_cast<char>(prefix.U8());
			const auto size = prefix.U32();
			const auto hash = prefix.U64();
			if ((type != 'M' && type != 'P') || size > MaxRecordBytes ||
			    offset + 13 + size > contents.size()) {
				break;
			}
			const auto payload = std::span(contents).subspan(offset + 13, size);
			if (XXH3_64bits(payload.data(), payload.size()) != hash) {
				break;
			}
			if (type == 'M') {
				if (size < 16 || (size - 16) % 4 != 0) {
					break;
				}
				const auto key = BytesKey(payload.first(16));
				if (known_modules.insert(key).second && m_mode == Mode::RecordAndReplay) {
					std::vector<uint32_t> words((size - 16) / 4);
					std::memcpy(words.data(), payload.data() + 16, words.size() * 4);
					module_words.emplace(key, std::move(words));
				}
			} else {
				auto key = BytesKey(payload);
				if (recorded.insert(key).second && entries.size() < MaxRecipes) {
					index.emplace(std::move(key), entries.size());
					entries.push_back({.bytes = {payload.begin(), payload.end()}});
				}
			}
			offset += 13 + size;
			good_end = offset;
		}
		if (entries.size() >= MaxRecipes) {
			reason      = "too many recipes";
			header_good = false;
		}
	}
	if (!header_good) {
		entries.clear();
		index.clear();
		recorded.clear();
		known_modules.clear();
		module_words.clear();
		good_end = 0;
		if (reason != nullptr) {
			PrewarmLog("Pipeline prewarm: starting {} over ({})", m_path.string(), reason);
		}
	} else if (good_end < contents.size()) {
		PrewarmLog("Pipeline prewarm: dropping {} damaged trailing bytes", contents.size() - good_end);
	}
	contents.clear();
	contents.shrink_to_fit();

	std::filesystem::create_directories(m_path.parent_path(), error);
	std::FILE* file = nullptr;
	if (good_end == 0) {
		file = std::fopen(m_path.string().c_str(), "wb");
		if (file != nullptr && std::fwrite(header.data(), 1, header.size(), file) != header.size()) {
			std::fclose(file);
			file = nullptr;
		}
	} else {
		std::filesystem::resize_file(m_path, good_end, error);
		file = error ? nullptr : std::fopen(m_path.string().c_str(), "ab");
	}
	if (file == nullptr) {
		PrewarmLog("Pipeline prewarm: cannot write {}; recording disabled", m_path.string());
	} else {
		std::fflush(file);
	}

	{
		std::scoped_lock lock(m_mutex);
		m_entries       = std::move(entries);
		m_index         = std::move(index);
		m_recorded      = std::move(recorded);
		m_known_modules = std::move(known_modules);
		m_file          = file;
		m_counters.loaded.store(m_entries.size(), std::memory_order_relaxed);
		m_loaded = true;
	}
	{
		std::scoped_lock lock(m_module_mutex);
		m_module_words = std::move(module_words);
	}
	m_changed.notify_all();
	PrewarmLog("Pipeline prewarm: {} recipes, {} modules from {} ({})", m_entries.size(),
	           m_known_modules.size(), m_path.string(),
	           m_mode == Mode::Record ? "record only" : "record and replay");
}

vk::ShaderModule PipelinePrewarm::ModuleFor(const SpirvModuleId& id) {
	const auto       key = ModuleKey(id);
	std::scoped_lock lock(m_module_mutex);
	if (const auto found = m_modules.find(key); found != m_modules.end()) {
		return found->second;
	}
	const auto words = m_module_words.find(key);
	if (words == m_module_words.end()) {
		return nullptr;
	}
	const auto module = CompileSPV(words->second, m_graphics.device);
	if (module != nullptr) {
		m_modules.emplace(key, module);
	}
	return module;
}

void PipelinePrewarm::BuildEntries(std::stop_token stop) {
	while (!stop.stop_requested()) {
		const size_t index = m_next_entry.fetch_add(1, std::memory_order_relaxed);
		if (index >= m_entries.size()) {
			break;
		}
		auto& entry = m_entries[index];
		{
			std::scoped_lock lock(m_mutex);
			if (entry.state != State::Queued) {
				continue;
			}
			entry.state = State::Building;
		}
		const auto              begin = SteadyNs();
		PipelineRecipe          recipe;
		PipelineCache::Pipeline pipeline;
		bool                    built = PipelineRecipe::Deserialize(entry.bytes, recipe);
		std::vector<vk::ShaderModule> modules;
		for (const auto& stage: recipe.stages) {
			if (!built) {
				break;
			}
			const auto module = ModuleFor(stage.module);
			built             = module != nullptr;
			modules.push_back(module);
		}
		built = built && CreatePipelineFromRecipe(m_graphics, recipe, modules, m_driver_cache, pipeline);
		const auto end = SteadyNs();
		m_counters.build_ns.fetch_add(end - begin, std::memory_order_relaxed);
		uint64_t expected = 0;
		m_counters.first_build_ns.compare_exchange_strong(expected, end - m_start_ns);
		m_counters.last_build_ns.store(end - m_start_ns, std::memory_order_relaxed);
		{
			std::scoped_lock lock(m_mutex);
			if (built) {
				entry.pipeline = pipeline;
				entry.state    = State::Ready;
				m_counters.built.fetch_add(1, std::memory_order_relaxed);
			} else {
				entry.state = State::Failed;
				m_counters.failed.fetch_add(1, std::memory_order_relaxed);
			}
		}
		m_changed.notify_all();
	}
}

bool PipelinePrewarm::Take(std::span<const uint8_t> recipe, vk::Pipeline& out,
                           std::chrono::milliseconds wait) {
	if (m_mode != Mode::RecordAndReplay) {
		return false;
	}
	std::unique_lock lock(m_mutex);
	m_changed.wait(lock, [this] { return m_loaded; });
	const auto found = m_index.find(BytesKey(recipe));
	if (found == m_index.end()) {
		m_counters.misses.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	auto& entry = m_entries[found->second];
	if (entry.state == State::Queued) {
		entry.state = State::Abandoned;
		m_counters.abandoned.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	if (entry.state == State::Building) {
		const auto begin = SteadyNs();
		m_changed.wait_for(lock, wait, [&] { return entry.state != State::Building; });
		m_counters.waited.fetch_add(1, std::memory_order_relaxed);
		m_counters.wait_ns.fetch_add(SteadyNs() - begin, std::memory_order_relaxed);
	}
	if (entry.state != State::Ready) {
		return false;
	}
	// The pipeline leaves; its layouts stay with the entry until the prewarm is destroyed.
	out                     = entry.pipeline.pipeline;
	entry.pipeline.pipeline = nullptr;
	entry.state             = State::Taken;
	m_counters.taken.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool PipelinePrewarm::AppendRecord(char type, std::span<const uint8_t> payload) {
	if (m_file == nullptr || payload.size() > MaxRecordBytes) {
		return false;
	}
	Writer prefix;
	prefix.U8(static_cast<uint8_t>(type));
	prefix.U32(static_cast<uint32_t>(payload.size()));
	prefix.U64(XXH3_64bits(payload.data(), payload.size()));
	const auto bytes = prefix.Take();
	if (std::fwrite(bytes.data(), 1, bytes.size(), m_file) != bytes.size() ||
	    std::fwrite(payload.data(), 1, payload.size(), m_file) != payload.size() ||
	    std::fflush(m_file) != 0) {
		std::fclose(m_file);
		m_file = nullptr;
		PrewarmLog("Pipeline prewarm: write failed; recording disabled");
		return false;
	}
	return true;
}

void PipelinePrewarm::NoteModule(const SpirvModuleId& id, std::span<const uint32_t> words) {
	if (m_mode == Mode::Off || !id.Valid()) {
		return;
	}
	std::unique_lock lock(m_mutex);
	m_changed.wait(lock, [this] { return m_loaded; });
	auto key = ModuleKey(id);
	if (m_known_modules.contains(key)) {
		return;
	}
	std::vector<uint8_t> payload(16 + words.size_bytes());
	std::memcpy(payload.data(), key.data(), 16);
	std::memcpy(payload.data() + 16, words.data(), words.size_bytes());
	if (AppendRecord('M', payload)) {
		m_known_modules.insert(std::move(key));
		m_counters.modules_recorded.fetch_add(1, std::memory_order_relaxed);
	}
}

void PipelinePrewarm::Record(std::span<const uint8_t> recipe) {
	if (m_mode == Mode::Off) {
		return;
	}
	std::unique_lock lock(m_mutex);
	m_changed.wait(lock, [this] { return m_loaded; });
	auto key = BytesKey(recipe);
	if (m_recorded.contains(key)) {
		return;
	}
	if (AppendRecord('P', recipe)) {
		m_recorded.insert(std::move(key));
		m_counters.recorded.fetch_add(1, std::memory_order_relaxed);
	}
}

void PipelinePrewarm::DestroyPipeline(PipelineCache::Pipeline& pipeline) {
	if (pipeline.pipeline != nullptr) {
		m_graphics.device.destroyPipeline(pipeline.pipeline, nullptr);
	}
	if (pipeline.pipeline_layout != nullptr) {
		m_graphics.device.destroyPipelineLayout(pipeline.pipeline_layout, nullptr);
	}
	if (pipeline.descriptor_set_layout != nullptr) {
		m_graphics.device.destroyDescriptorSetLayout(pipeline.descriptor_set_layout, nullptr);
	}
	pipeline = {};
}

void PipelinePrewarm::LogSummary() {
	if (m_mode == Mode::Off) {
		return;
	}
	const auto& c = m_counters;
	PrewarmLog("Pipeline prewarm: loaded {} built {} failed {} (worker {:.0f} ms, first {:.0f} ms, "
	           "last {:.0f} ms after start) | taken {} waited {} ({:.0f} ms) abandoned {} misses {} | "
	           "recorded {} recipes, {} modules",
	           c.loaded.load(), c.built.load(), c.failed.load(),
	           static_cast<double>(c.build_ns.load()) / 1e6,
	           static_cast<double>(c.first_build_ns.load()) / 1e6,
	           static_cast<double>(c.last_build_ns.load()) / 1e6, c.taken.load(), c.waited.load(),
	           static_cast<double>(c.wait_ns.load()) / 1e6, c.abandoned.load(), c.misses.load(),
	           c.recorded.load(), c.modules_recorded.load());
}

} // namespace Libs::Graphics
