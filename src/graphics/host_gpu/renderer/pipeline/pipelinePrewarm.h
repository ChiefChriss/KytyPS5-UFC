#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINEPREWARM_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINEPREWARM_H_

#include "graphics/host_gpu/renderer/pipeline/pipelineCache.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Libs::Graphics {

struct GraphicContext;

// Everything one vkCreate*Pipelines call and its two layouts consume, with every device-dependent
// choice already resolved. Equal serialized bytes create identical Vulkan objects on the same
// device and feature set, so the bytes are the prewarm identity; guest state is not involved.
struct PipelineRecipe {
	enum class Kind : uint8_t { Graphics = 1, Compute = 2 };

	struct Stage {
		vk::ShaderStageFlagBits stage                  = vk::ShaderStageFlagBits::eVertex;
		SpirvModuleId           module;
		uint32_t                required_subgroup_size = 0;
	};

	Kind                                        kind = Kind::Graphics;
	std::vector<Stage>                          stages;
	std::vector<vk::DescriptorSetLayoutBinding> descriptors;
	bool                                        push_descriptors     = false;
	vk::ShaderStageFlags                        push_constant_stages = {};
	uint32_t                                    push_constant_size   = 0;

	// Graphics only.
	bool                                               mesh = false;
	std::vector<vk::VertexInputBindingDescription>     vertex_bindings;
	std::vector<vk::VertexInputAttributeDescription>   vertex_attributes;
	vk::PrimitiveTopology                              topology             = vk::PrimitiveTopology::ePointList;
	bool                                               primitive_restart    = false;
	bool                                               tessellation_state   = false;
	uint32_t                                           patch_control_points = 0;
	bool                                               negative_one_to_one  = false;
	bool                                               depth_clamp          = false;
	bool                                               depth_clip_struct    = false;
	bool                                               depth_clip_enable    = false;
	bool                                               provoking_struct     = false;
	vk::ProvokingVertexModeEXT                         provoking_mode       = vk::ProvokingVertexModeEXT::eFirstVertex;
	vk::CullModeFlags                                  cull_mode            = {};
	vk::FrontFace                                      front_face           = vk::FrontFace::eCounterClockwise;
	vk::PolygonMode                                    polygon_mode         = vk::PolygonMode::eFill;
	vk::SampleCountFlagBits                            samples              = vk::SampleCountFlagBits::e1;
	bool                                               sample_shading       = false;
	bool                                               depth_stencil_state  = false;
	bool                                               depth_bounds_test    = false;
	float                                              min_depth_bounds     = 0.0f;
	float                                              max_depth_bounds     = 0.0f;
	std::vector<vk::PipelineColorBlendAttachmentState> blend_attachments;
	bool                                               color_write_struct = false;
	std::array<vk::Format, RENDER_COLOR_ATTACHMENTS_MAX> color_formats {};
	uint32_t                                           color_count    = 0;
	vk::Format                                         depth_format   = vk::Format::eUndefined;
	vk::Format                                         stencil_format = vk::Format::eUndefined;
	std::vector<vk::DynamicState>                      dynamic_states;

	[[nodiscard]] std::vector<uint8_t> Serialize() const;
	[[nodiscard]] static bool          Deserialize(std::span<const uint8_t> bytes, PipelineRecipe& recipe);
};

// Creates the descriptor set layout, pipeline layout and pipeline that `recipe` describes, with
// `modules[i]` supplying stage i. Returns false (and leaves `out` empty) when Vulkan fails.
// `create_ns` receives the time spent in vkCreate*Pipelines alone.
bool CreatePipelineFromRecipe(GraphicContext& graphics, const PipelineRecipe& recipe,
                              std::span<const vk::ShaderModule> modules,
                              vk::PipelineCache driver_cache, PipelineCache::Pipeline& out,
                              uint64_t* create_ns = nullptr);

// KYTY_UFC_PIPELINE_PREWARM: unset/1 record and replay, "record" record only, 0 off.
// Every pipeline created during play is appended (with its SPIR-V modules) to
// _PipelineCache/<title>.prewarm as it is created, so a crashed or killed run keeps its list. The
// next launch rebuilds the list on background threads (KYTY_UFC_PIPELINE_PREWARM_THREADS, default
// a quarter of the logical processors, 1..4) and a draw or dispatch whose recipe matches takes
// the prebuilt pipeline instead of compiling it on the GPU thread.
class PipelinePrewarm {
public:
	enum class Mode { Off, Record, RecordAndReplay };
	[[nodiscard]] static Mode ConfiguredMode();

	PipelinePrewarm(GraphicContext& graphics, vk::PipelineCache driver_cache,
	                const std::string& title_id, Mode mode);
	~PipelinePrewarm();
	PipelinePrewarm(const PipelinePrewarm&)            = delete;
	PipelinePrewarm& operator=(const PipelinePrewarm&) = delete;

	// Records a module once so later recipes can reference it.
	void NoteModule(const SpirvModuleId& id, std::span<const uint32_t> words);
	// Claims the prebuilt pipeline for `recipe`, waiting if a worker is building it right now.
	bool Take(std::span<const uint8_t> recipe, PipelineCache::Pipeline& out);
	// Appends a recipe the live path just built.
	void Record(std::span<const uint8_t> recipe);
	void LogSummary();

private:
	enum class State : uint8_t { Queued, Building, Ready, Taken, Abandoned, Failed };
	struct Entry {
		std::vector<uint8_t>    bytes;
		State                   state = State::Queued;
		PipelineCache::Pipeline pipeline;
	};

	void Load();
	void BuildEntries(std::stop_token stop);
	bool AppendRecord(char type, std::span<const uint8_t> payload);
	vk::ShaderModule ModuleFor(const SpirvModuleId& id);
	void DestroyPipeline(PipelineCache::Pipeline& pipeline);
	[[nodiscard]] std::string Signature() const;

	GraphicContext&       m_graphics;
	vk::PipelineCache     m_driver_cache;
	Mode                  m_mode;
	std::filesystem::path m_path;
	std::FILE*            m_file = nullptr;

	std::mutex                                 m_mutex;
	std::condition_variable                    m_changed;
	bool                                       m_loaded = false;
	std::vector<Entry>                         m_entries;
	std::unordered_map<std::string, size_t>    m_index;
	std::unordered_set<std::string>            m_recorded;
	std::mutex                                               m_module_mutex;
	std::unordered_map<std::string, std::vector<uint32_t>>   m_module_words;
	std::unordered_map<std::string, vk::ShaderModule>        m_modules;
	std::unordered_set<std::string>            m_known_modules;
	std::atomic<size_t>                        m_next_entry {0};
	std::atomic<uint32_t>                      m_active_builders {0};

	std::jthread              m_loader;
	std::vector<std::jthread> m_builders;

	struct Counters {
		std::atomic<uint64_t> loaded {0}, built {0}, failed {0}, build_ns {0};
		std::atomic<uint64_t> taken {0}, waited {0}, wait_ns {0}, abandoned {0}, misses {0};
		std::atomic<uint64_t> recorded {0}, modules_recorded {0};
		std::atomic<uint64_t> first_build_ns {0}, last_build_ns {0};
	} m_counters;
	uint64_t m_start_ns = 0;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_PIPELINEPREWARM_H_
