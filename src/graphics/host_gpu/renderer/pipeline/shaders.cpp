#include "common/assert.h"
#include "common/common.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/guest_gpu/gpu_defs.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/pipeline/descriptors.h"
#include "graphics/host_gpu/renderer/pipeline/pipelineCache.h"
#include "graphics/host_gpu/renderer/pipeline/pipelinePrewarm.h"
#include "graphics/host_gpu/renderer/pipeline/shaderResourceBarrier.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/host_gpu/renderer/renderTarget.h"
#include "graphics/host_gpu/vulkanCommon.h"
#include "graphics/shader/recompiler/BufferFormat.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/rectListShader.h"
#include "graphics/shader/shader.h"

#include <chrono>
#include <algorithm>
#include <limits>
#include <span>
#include <vector>

namespace Libs::Graphics {

// IDK: maybe we can remove it?
constexpr uint8_t kTemporaryVertexAttribFormat113 =
    static_cast<uint8_t>(Prospero::VertexAttribFormat::k16_16SInt);
constexpr uint32_t kTemporaryPs5BufferFormat121 = 121u;

static bool NarrowInputFormat(vk::Format& format, uint32_t& size, uint32_t used_components) {
	if (used_components == 0 || used_components >= size) {
		return false;
	}

	switch (format) {
		case vk::Format::eR32G32B32A32Sfloat:
			switch (used_components) {
				case 1: format = vk::Format::eR32Sfloat; break;
				case 2: format = vk::Format::eR32G32Sfloat; break;
				case 3: format = vk::Format::eR32G32B32Sfloat; break;
				default: return false;
			}
			size = used_components;
			return true;
		case vk::Format::eR32G32B32Sfloat:
			switch (used_components) {
				case 1: format = vk::Format::eR32Sfloat; break;
				case 2: format = vk::Format::eR32G32Sfloat; break;
				default: return false;
			}
			size = used_components;
			return true;
		case vk::Format::eR16G16B16A16Sfloat:
			switch (used_components) {
				case 1: format = vk::Format::eR16Sfloat; break;
				case 2: format = vk::Format::eR16G16Sfloat; break;
				default: return false;
			}
			size = used_components;
			return true;
		case vk::Format::eR8G8B8A8Unorm:
			switch (used_components) {
				case 1: format = vk::Format::eR8Unorm; break;
				case 2: format = vk::Format::eR8G8Unorm; break;
				default: return false;
			}
			size = used_components;
			return true;
		case vk::Format::eR8G8B8A8Snorm:
			if (used_components != 2) {
				return false;
			}
			format = vk::Format::eR8G8Snorm;
			size   = 2;
			return true;
		case vk::Format::eR8G8B8A8Uint:
			switch (used_components) {
				case 1: format = vk::Format::eR8Uint; break;
				case 2: format = vk::Format::eR8G8Uint; break;
				default: return false;
			}
			size = used_components;
			return true;
		default: break;
	}

	return false;
}

static void GetInputFormat(const ShaderBufferResource& res, vk::Format& format, uint32_t& size,
                           uint32_t used_components) {
	const auto fmt        = res.Format();
	const auto raw_format = res.RawFormat();
	if (raw_format == kTemporaryVertexAttribFormat113) {
		static bool logged_113 = false;
		if (!logged_113) {
			LOGF("InputFormat: temporary: accepting invalid PS5 buffer format 113 as "
			     "vk::Format::eR32G32B32A32Sfloat\n");
			logged_113 = true;
		}
		format = vk::Format::eR32G32B32A32Sfloat;
		size   = 4;
		if (NarrowInputFormat(format, size, used_components)) {
			LOGF("InputFormat: narrowing fmt=%u to %s for used_components=%u\n", raw_format,
			     vk::to_string(format).c_str(), used_components);
		}
		return;
	}
	if (raw_format == kTemporaryPs5BufferFormat121) {
		static bool logged_121 = false;
		if (!logged_121) {
			LOGF("InputFormat: accepting PS5 buffer format 121 as vk::Format::eR16G16Sfloat\n");
			logged_121 = true;
		}
		format = vk::Format::eR16G16Sfloat;
		size   = 2;
		return;
	}

	format = VulkanFormat(fmt);
	size   = ShaderRecompiler::Format::GetFormatInfo(fmt).component_count;
	if (format == vk::Format::eUndefined || size == 0) {
		EXIT("unknown vertex format: fmt = %u\n", raw_format);
	}

	if (NarrowInputFormat(format, size, used_components)) {
		static std::atomic<uint64_t> log_count = 0;
		auto                         log_id    = log_count.fetch_add(1);
		if (log_id < 32) {
			LOGF("VertexInput: narrowed vertex format to %" PRIu32
			     " component(s) for shader fetch\n",
			     used_components);
		}
	}
}

static vk::BlendFactor GetBlendFactor(uint32_t factor, bool remap_source_alpha) {
	switch (static_cast<Prospero::BlendFactor>(factor)) {
		case Prospero::BlendFactor::kZero: return vk::BlendFactor::eZero;
		case Prospero::BlendFactor::kOne: return vk::BlendFactor::eOne;
		case Prospero::BlendFactor::kSrcColor: return vk::BlendFactor::eSrcColor;
		case Prospero::BlendFactor::kOneMinusSrcColor: return vk::BlendFactor::eOneMinusSrcColor;
		case Prospero::BlendFactor::kSrcAlpha:
			return remap_source_alpha ? vk::BlendFactor::eSrc1Color : vk::BlendFactor::eSrcAlpha;
		case Prospero::BlendFactor::kOneMinusSrcAlpha:
			return remap_source_alpha ? vk::BlendFactor::eOneMinusSrc1Color
			                          : vk::BlendFactor::eOneMinusSrcAlpha;
		case Prospero::BlendFactor::kDstAlpha: return vk::BlendFactor::eDstAlpha;
		case Prospero::BlendFactor::kOneMinusDstAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
		case Prospero::BlendFactor::kDstColor: return vk::BlendFactor::eDstColor;
		case Prospero::BlendFactor::kOneMinusDstColor: return vk::BlendFactor::eOneMinusDstColor;
		case Prospero::BlendFactor::kSrcAlphaSaturate: return vk::BlendFactor::eSrcAlphaSaturate;
		case Prospero::BlendFactor::kConstantColor: return vk::BlendFactor::eConstantColor;
		case Prospero::BlendFactor::kOneMinusConstantColor:
			return vk::BlendFactor::eOneMinusConstantColor;
		case Prospero::BlendFactor::kSrc1Color: return vk::BlendFactor::eSrc1Color;
		case Prospero::BlendFactor::kOneMinusSrc1Color: return vk::BlendFactor::eOneMinusSrc1Color;
		case Prospero::BlendFactor::kSrc1Alpha: return vk::BlendFactor::eSrc1Alpha;
		case Prospero::BlendFactor::kOneMinusSrc1Alpha: return vk::BlendFactor::eOneMinusSrc1Alpha;
		case Prospero::BlendFactor::kConstantAlpha: return vk::BlendFactor::eConstantAlpha;
		case Prospero::BlendFactor::kOneMinusConstantAlpha:
			return vk::BlendFactor::eOneMinusConstantAlpha;
		default: EXIT("unknown factor: %u\n", factor);
	}
	return vk::BlendFactor::eZero;
}

static vk::BlendOp GetBlendOp(uint32_t op) {
	switch (static_cast<Prospero::BlendOp>(op)) {
		case Prospero::BlendOp::kAdd: return vk::BlendOp::eAdd;
		case Prospero::BlendOp::kSubtract: return vk::BlendOp::eSubtract;
		case Prospero::BlendOp::kMin: return vk::BlendOp::eMin;
		case Prospero::BlendOp::kMax: return vk::BlendOp::eMax;
		case Prospero::BlendOp::kReverseSubtract: return vk::BlendOp::eReverseSubtract;
		default: EXIT("unknown op: %u\n", op);
	}
	return vk::BlendOp::eAdd;
}

static void AddLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>&    descriptor_bindings,
                              const ShaderRecompiler::IR::CompiledShaderInfo& program,
                              vk::ShaderStageFlagBits                         stage) {
	for (const auto& binding: program.bindings.descriptors) {
		descriptor_bindings.push_back(
		    {ShaderRecompiler::IR::NativeBinding(program.stage, binding.kind),
		     NativeDescriptorType(binding.kind), NativeDescriptorCount(binding), stage, nullptr});
	}
}

static bool UsesPushDescriptors(const GraphicContext&                          graphics,
                                std::span<const vk::DescriptorSetLayoutBinding> bindings) {
	uint32_t descriptor_count = 0;
	for (const auto& binding: bindings) {
		descriptor_count += binding.descriptorCount;
	}
	return descriptor_count <= graphics.max_push_descriptors;
}

// Every stage needs a recorded SPIR-V identity before its pipeline can be prewarmed.
static bool RecipeIsReplayable(const PipelineRecipe& recipe) {
	return std::ranges::all_of(recipe.stages,
	                           [](const auto& stage) { return stage.module.Valid(); });
}

// Takes the prebuilt pipeline for `recipe`, or creates it and records the recipe for next launch.
static void CreateOrTakePipeline(GraphicContext& graphics, PipelineCache::Pipeline& pipeline,
                                 const PipelineRecipe&             recipe,
                                 std::span<const vk::ShaderModule> modules,
                                 vk::PipelineCache driver_cache, PipelinePrewarm* prewarm) {
	EXIT_IF(pipeline.pipeline_layout != nullptr || pipeline.pipeline != nullptr);
	const bool replayable = prewarm != nullptr && RecipeIsReplayable(recipe);
	const auto bytes      = replayable ? recipe.Serialize() : std::vector<uint8_t> {};
	if (replayable && prewarm->Take(bytes, pipeline)) {
		return;
	}
	uint64_t   create_ns = 0;
	const bool created =
	    CreatePipelineFromRecipe(graphics, recipe, modules, driver_cache, pipeline, &create_ns);
	NotePipelineBuildNs(create_ns);
	EXIT_NOT_IMPLEMENTED(!created);
	EXIT_NOT_IMPLEMENTED(pipeline.pipeline == nullptr);
	EXIT_NOT_IMPLEMENTED(pipeline.pipeline_layout == nullptr);
	if (replayable) {
		prewarm->Record(bytes);
	}
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void CreatePipelineInternal(GraphicContext& graphics, PipelineCache::Pipeline& pipeline,
                            const PipelineRenderingState&          rendering,
                            const PipelineVertexInputState&        vertex_input,
                            std::span<const ShaderVertexInputInfo> vertex_info,
                            const ShaderPixelInputInfo*            ps_input_info,
                            const PipelineCache::GraphicsPrograms& programs,
                            const PipelineStaticParameters&        static_params,
                            vk::PipelineCache driver_cache, PipelinePrewarm* prewarm) {
	const auto& vs_input_info  = vertex_info.front();
	const auto& vertex_program = programs.vertex[0];
	const auto& pixel_program  = programs.pixel;
	const bool  tessellation   = vertex_info.size() == 3;
	const bool  ps_active      = ps_input_info != nullptr;
	EXIT_IF(!vertex_program || (ps_active && !pixel_program));
	const bool with_depth = rendering.depth_format != vk::Format::eUndefined ||
	                        rendering.stencil_format != vk::Format::eUndefined;
	EXIT_IF(!vs_input_info.stage);
	const bool mesh = vs_input_info.stage.program->stage == ShaderType::Mesh;
	EXIT_NOT_IMPLEMENTED(mesh && !graphics.mesh_shader_enabled);
	const bool rect_list =
	    !mesh && !tessellation && static_params.topology == vk::PrimitiveTopology::ePatchList;

	vk::ShaderModule tess_control_shader_module = nullptr;
	vk::ShaderModule tess_eval_shader_module    = nullptr;
	SpirvModuleId    tess_control_id;
	SpirvModuleId    tess_eval_id;

	if (rect_list) {
		const auto shaders =
		    BuildRectListShaders(vs_input_info, ps_active ? ps_input_info : nullptr);
		tess_control_shader_module = CompileSPV(shaders.control, graphics.device);
		if (graphics_debug_dump_enabled()) {
			LOGF("PipelineTrace: vkCreateShaderModule RectList TCS done module=%p\n",
			     static_cast<void*>(tess_control_shader_module));
		}

		tess_eval_shader_module = CompileSPV(shaders.evaluation, graphics.device);
		if (graphics_debug_dump_enabled()) {
			LOGF("PipelineTrace: vkCreateShaderModule RectList TES done module=%p\n",
			     static_cast<void*>(tess_eval_shader_module));
		}
		tess_control_id = SpirvModuleId::Of(shaders.control);
		tess_eval_id    = SpirvModuleId::Of(shaders.evaluation);
		if (prewarm != nullptr) {
			prewarm->NoteModule(tess_control_id, shaders.control);
			prewarm->NoteModule(tess_eval_id, shaders.evaluation);
		}
	}

	EXIT_NOT_IMPLEMENTED(
	    rect_list && (tess_control_shader_module == nullptr || tess_eval_shader_module == nullptr));

	PipelineRecipe recipe;
	recipe.kind = PipelineRecipe::Kind::Graphics;
	recipe.mesh = mesh;
	std::vector<vk::ShaderModule> modules;
	for (uint32_t i = 0; i < vertex_info.size(); i++) {
		recipe.stages.push_back({.stage  = NativeShaderStage(vertex_info[i].logical_stage),
		                         .module = programs.vertex[i].spirv});
		modules.push_back(programs.vertex[i].module);
	}
	if (rect_list) {
		recipe.stages.push_back(
		    {.stage = vk::ShaderStageFlagBits::eTessellationControl, .module = tess_control_id});
		modules.push_back(tess_control_shader_module);
		recipe.stages.push_back(
		    {.stage = vk::ShaderStageFlagBits::eTessellationEvaluation, .module = tess_eval_id});
		modules.push_back(tess_eval_shader_module);
	}
	if (ps_active) {
		recipe.stages.push_back(
		    {.stage = vk::ShaderStageFlagBits::eFragment, .module = pixel_program.spirv});
		modules.push_back(pixel_program.module);
	}

	for (uint32_t binding = 0; binding < vertex_input.binding_count; binding++) {
		recipe.vertex_bindings.push_back(
		    {.binding   = binding,
		     .stride    = vertex_input.bindings[binding].stride,
		     .inputRate = vertex_input.bindings[binding].instance ? vk::VertexInputRate::eInstance
		                                                          : vk::VertexInputRate::eVertex});
	}
	for (uint32_t index = 0; index < vertex_input.attribute_count; index++) {
		vk::VertexInputAttributeDescription attribute {};
		attribute.binding  = vertex_input.attributes[index].binding;
		attribute.location = index;
		attribute.offset   = vertex_input.attributes[index].offset;

		uint32_t   attr_size     = 4;
		const auto registers_num = vs_input_info.resources_dst[index].registers_num;
		const auto compiled_components =
		    vs_input_info.stage.program->info.vertex_fetch_components[index];
		const auto used_components =
		    compiled_components > 0 ? static_cast<int>(compiled_components) : registers_num;
		GetInputFormat(vs_input_info.resources[index], attribute.format, attr_size,
		               static_cast<uint32_t>(used_components));

		if (graphics_debug_dump_enabled()) {
			static std::atomic_uint log_count = 0;
			const auto              log_id    = log_count.fetch_add(1, std::memory_order_relaxed);
			if (log_id < 128) {
				LOGF("VertexInputState[%u]: attr=%u binding=%u offset=%u stride=%u fmt=%d "
				     "src_fmt=%u dst=v%u regs=%u"
				     " fetched_components=%u attr_size=%u swizzle=%u,%u,%u,%u\n",
				     log_id, index, attribute.binding, attribute.offset,
				     vertex_input.bindings[attribute.binding].stride,
				     static_cast<int>(attribute.format),
				     static_cast<uint32_t>(vs_input_info.resources[index].Format()),
				     static_cast<uint32_t>(vs_input_info.resources_dst[index].register_start),
				     static_cast<uint32_t>(registers_num), static_cast<uint32_t>(used_components),
				     attr_size, static_cast<uint32_t>(vs_input_info.resources[index].DstSelX()),
				     static_cast<uint32_t>(vs_input_info.resources[index].DstSelY()),
				     static_cast<uint32_t>(vs_input_info.resources[index].DstSelZ()),
				     static_cast<uint32_t>(vs_input_info.resources[index].DstSelW()));
			}
		}

		if (vs_input_info.resources[index].OutOfBounds() != 0) {
			static bool logged = false;
			if (!logged) {
				LOGF("VertexInput: temporary: accepting PS5 out-of-bounds behavior %" PRIu8 "\n",
				     vs_input_info.resources[index].OutOfBounds());
				logged = true;
			}
		}

		EXIT_IF(registers_num < 1 || registers_num > 4);
		recipe.vertex_attributes.push_back(attribute);
	}

	recipe.topology            = static_params.topology;
	recipe.primitive_restart   = static_params.primitive_restart_enable;
	recipe.negative_one_to_one = static_params.negative_one_to_one;

	if (static_params.cull_back) {
		recipe.cull_mode |= vk::CullModeFlagBits::eBack;
	}
	if (static_params.cull_front) {
		recipe.cull_mode |= vk::CullModeFlagBits::eFront;
	}
	recipe.front_face =
	    (static_params.face ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise);

	recipe.depth_clip_enable = static_params.depth_clip_enable;
	// MoltenVK lacks VK_EXT_depth_clip_enable; omit the depth-clip struct on macOS and accept
	// Vulkan's default depth clipping (enabled) instead of the PS5's clamp behavior.
#if !defined(__APPLE__)
	// The DB clamps depth to the viewport range after polygon offset is applied.
	recipe.depth_clamp       = true;
	recipe.depth_clip_struct = true;
#endif
	EXIT_NOT_IMPLEMENTED(static_params.provoking_vtx_last &&
	                     !graphics.provoking_vertex_last_enabled);
	if (graphics.provoking_vertex_last_enabled) {
		recipe.provoking_struct = true;
		recipe.provoking_mode   = static_params.provoking_vtx_last
		                              ? vk::ProvokingVertexModeEXT::eLastVertex
		                              : vk::ProvokingVertexModeEXT::eFirstVertex;
	}
	recipe.polygon_mode = static_params.polygon_mode;

	recipe.sample_shading = static_params.sample_shading_enable;
	recipe.samples        = vulkan_sample_count(static_params.samples);

	for (uint32_t i = 0; i < rendering.color_count; i++) {
		EXIT_NOT_IMPLEMENTED((static_params.color_mask[i] & ~0x0fu) != 0);
		vk::PipelineColorBlendAttachmentState blend {};
		blend.colorWriteMask = vk::ColorComponentFlags {static_params.color_mask[i]};
		blend.blendEnable    = static_params.blend_enable[i] ? VK_TRUE : VK_FALSE;
		// Only target 0 can use the second blend source carrying logical alpha.
		const bool remap_source_alpha = i == 0 && static_params.blend_alpha_source_remap;
		blend.srcColorBlendFactor =
		    GetBlendFactor(static_params.color_srcblend[i], remap_source_alpha);
		blend.dstColorBlendFactor =
		    GetBlendFactor(static_params.color_destblend[i], remap_source_alpha);
		blend.colorBlendOp = GetBlendOp(static_params.color_comb_fcn[i]);
		blend.srcAlphaBlendFactor =
		    (static_params.separate_alpha_blend[i]
		         ? GetBlendFactor(static_params.alpha_srcblend[i], remap_source_alpha)
		         : blend.srcColorBlendFactor);
		blend.dstAlphaBlendFactor =
		    (static_params.separate_alpha_blend[i]
		         ? GetBlendFactor(static_params.alpha_destblend[i], remap_source_alpha)
		         : blend.dstColorBlendFactor);
		blend.alphaBlendOp = (static_params.separate_alpha_blend[i]
		                          ? GetBlendOp(static_params.alpha_comb_fcn[i])
		                          : blend.colorBlendOp);
		recipe.blend_attachments.push_back(blend);
	}
	// MoltenVK lacks VK_EXT_color_write_enable; drop the dynamic color-write struct on macOS
	// and rely on each attachment's static colorWriteMask (all channels enabled by default).
#if !defined(__APPLE__)
	recipe.color_write_struct = true;
#endif

	vk::ShaderStageFlags graphics_stages = vk::ShaderStageFlagBits::eFragment;
	for (const auto& stage: vertex_info) {
		const auto native_stage = NativeShaderStage(stage.logical_stage);
		AddLayoutBindings(recipe.descriptors, *stage.stage.program, native_stage);
		graphics_stages |= native_stage;
	}
	if (ps_active) {
		EXIT_IF(!ps_input_info->stage);
		AddLayoutBindings(recipe.descriptors, *ps_input_info->stage.program,
		                  vk::ShaderStageFlagBits::eFragment);
	}
	recipe.push_descriptors     = UsesPushDescriptors(graphics, recipe.descriptors);
	recipe.push_constant_stages = graphics_stages;
	recipe.push_constant_size   = ShaderRecompiler::IR::NativePushConstantSize;

	recipe.depth_stencil_state = with_depth;
#if defined(__APPLE__)
	recipe.depth_bounds_test = false; // MoltenVK lacks the depthBounds feature
#else
	recipe.depth_bounds_test = static_params.depth_bounds_test_enable;
#endif
	recipe.min_depth_bounds = static_params.depth_min_bounds;
	recipe.max_depth_bounds = static_params.depth_max_bounds;

	recipe.dynamic_states = {
	    vk::DynamicState::eViewportWithCount,  vk::DynamicState::eScissorWithCount,
	    vk::DynamicState::eLineWidth,          vk::DynamicState::eDepthTestEnable,
	    vk::DynamicState::eDepthWriteEnable,   vk::DynamicState::eDepthCompareOp,
	    vk::DynamicState::eDepthBiasEnable,    vk::DynamicState::eDepthBias,
	    vk::DynamicState::eStencilTestEnable,  vk::DynamicState::eStencilOp,
	    vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilReference,
	    vk::DynamicState::eStencilWriteMask,   vk::DynamicState::eBlendConstants,
	};
#if !defined(__APPLE__)
	if (rendering.color_count != 0) {
		recipe.dynamic_states.push_back(vk::DynamicState::eColorWriteEnableEXT);
	}
#endif
	if (graphics.attachment_feedback_loop_enabled) {
		recipe.dynamic_states.push_back(vk::DynamicState::eAttachmentFeedbackLoopEnableEXT);
	}

	recipe.color_formats  = rendering.color_formats;
	recipe.color_count    = rendering.color_count;
	recipe.depth_format   = rendering.depth_format;
	recipe.stencil_format = rendering.stencil_format;

	recipe.tessellation_state   = rect_list || tessellation;
	recipe.patch_control_points = tessellation ? vs_input_info.tess.input_control_points : 3u;

	if (graphics_debug_dump_enabled()) {
		LOGF("PipelineTrace: vkCreateGraphicsPipelines begin VS=%" PRIu64 " PS=%" PRIu64
		     " topology=%" PRIu32 " color_mask=0x%08" PRIx32
		     " depth=%s blend=%s dyn_states=%" PRIu32 "\n",
		     vertex_program.id, ps_active ? pixel_program.id : 0,
		     static_cast<uint32_t>(static_params.topology), static_params.color_mask[0],
		     (with_depth ? "true" : "false"), (static_params.blend_enable[0] ? "true" : "false"),
		     static_cast<uint32_t>(recipe.dynamic_states.size()));
	}
	CreateOrTakePipeline(graphics, pipeline, recipe, modules, driver_cache, prewarm);
	if (graphics_debug_dump_enabled()) {
		LOGF("PipelineTrace: vkCreateGraphicsPipelines done pipeline=%p\n",
		     static_cast<void*>(pipeline.pipeline));
	}

	if (tess_control_shader_module != nullptr) {
		graphics.device.destroyShaderModule(tess_control_shader_module, nullptr);
	}
	if (tess_eval_shader_module != nullptr) {
		graphics.device.destroyShaderModule(tess_eval_shader_module, nullptr);
	}
}

void CreatePipelineInternal(GraphicContext& graphics, PipelineCache::Pipeline& pipeline,
                            const ShaderComputeInputInfo& input_info,
                            const ShaderProgram& compute_program, vk::PipelineCache driver_cache,
                            PipelinePrewarm* prewarm) {
	EXIT_IF(compute_program.module == nullptr);
	EXIT_IF(!input_info.stage);

	PipelineRecipe recipe;
	recipe.kind          = PipelineRecipe::Kind::Compute;
	const auto wave_size = input_info.stage.program->wave_size;
	const bool required_size = graphics.compute_subgroup_size_control_enabled &&
	                           wave_size >= graphics.min_subgroup_size &&
	                           wave_size <= graphics.max_subgroup_size;
	recipe.stages.push_back({.stage                  = vk::ShaderStageFlagBits::eCompute,
	                         .module                 = compute_program.spirv,
	                         .required_subgroup_size = required_size ? wave_size : 0u});
	AddLayoutBindings(recipe.descriptors, *input_info.stage.program,
	                  vk::ShaderStageFlagBits::eCompute);
	recipe.push_descriptors     = UsesPushDescriptors(graphics, recipe.descriptors);
	recipe.push_constant_stages = vk::ShaderStageFlagBits::eCompute;
	recipe.push_constant_size   = ShaderRecompiler::IR::NativePushConstantSize;

	LOGF("PipelineTrace: vkCreateComputePipelines begin\n");
	const std::array modules {compute_program.module};
	CreateOrTakePipeline(graphics, pipeline, recipe, modules, driver_cache, prewarm);
	LOGF("PipelineTrace: vkCreateComputePipelines done pipeline=%p\n",
	     static_cast<void*>(pipeline.pipeline));
}

} // namespace Libs::Graphics
