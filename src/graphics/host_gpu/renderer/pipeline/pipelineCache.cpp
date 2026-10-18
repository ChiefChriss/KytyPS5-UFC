#include "graphics/host_gpu/renderer/pipeline/pipelineCache.h"

#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/threads.h"
#include "graphics/guest_gpu/hardwareContext.h"
#include "graphics/host_gpu/renderer/colorRenderTarget.h"
#include "graphics/host_gpu/renderer/debug.h"
#include "graphics/host_gpu/renderer/depthRenderTarget.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"
#include "graphics/host_gpu/renderer/pipeline/blendMapping.h"
#include "graphics/host_gpu/renderer/pipeline/shaderDiskCache.h"
#include "graphics/host_gpu/renderer/render.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/NanTrace.h"
#include "graphics/shader/shaderCompiler.h"
#include "kernel/memory.h"
#include "kytyGitVersion.h"
#include "loader/systemContent.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <limits>
#include <optional>
#include <span>
#include <spirv-tools/libspirv.hpp>
#include <string_view>
#include <utility>
#include <vector>
#include <xxhash.h>

namespace Libs::Graphics {

namespace {

struct ShaderBuildTimes {
	std::atomic<uint64_t> translate_ns {0};
	std::atomic<uint64_t> compile_ns {0};
	std::atomic<uint64_t> validate_ns {0};
	std::atomic<uint64_t> module_ns {0};
	std::atomic<uint64_t> pipeline_ns {0};
	std::atomic<uint64_t> pipelines {0};
};
ShaderBuildTimes& BuildTimes() {
	static ShaderBuildTimes times;
	return times;
}
uint64_t SteadyNs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
	                                 std::chrono::steady_clock::now().time_since_epoch())
	                                 .count());
}

vk::PolygonMode ResolvePolygonMode(const HW::ModeControl& mode, bool cull_front, bool cull_back) {
	// CxPrimitiveSetup::PolygonMode disables both per-face modes when it is zero.
	if (mode.poly_mode == 0) {
		return vk::PolygonMode::eFill;
	}
	EXIT_NOT_IMPLEMENTED(mode.poly_mode != 1);
	if (cull_front && cull_back) {
		return vk::PolygonMode::eFill;
	}
	if (!cull_front && !cull_back && mode.polymode_front_ptype != mode.polymode_back_ptype) {
		EXIT("Pipeline: different polygon modes for two visible faces are unsupported\n");
	}
	// Vulkan has one polygon mode. A culled face does not constrain that mode.
	const auto polygon_mode = cull_front ? mode.polymode_back_ptype : mode.polymode_front_ptype;
	switch (polygon_mode) {
		case 0: return vk::PolygonMode::ePoint;
		case 1: return vk::PolygonMode::eLine;
		case 2: return vk::PolygonMode::eFill;
		default: EXIT("Pipeline: invalid polygon mode %u\n", polygon_mode);
	}
}

std::string DriverCacheSignature(const vk::PhysicalDeviceProperties& properties) {
	constexpr char hex[] = "0123456789abcdef";
	std::string    uuid(VK_UUID_SIZE * 2, '0');
	for (size_t i = 0; i < VK_UUID_SIZE; i++) {
		uuid[i * 2]     = hex[properties.pipelineCacheUUID[i] >> 4u];
		uuid[i * 2 + 1] = hex[properties.pipelineCacheUUID[i] & 0xfu];
	}
	return fmt::format("KytyPC2:{:08x}:{:08x}:{:08x}:{}\n",
	                   properties.vendorID, properties.deviceID, properties.driverVersion, uuid);
}

std::string PipelineCacheTitleId() {
	std::string title_id;
	if ((!Loader::SystemContentParamSfoGetString("TITLE_ID", &title_id) || title_id.empty()) &&
	    (!Loader::SystemContentParamSfoGetString("CONTENT_ID", &title_id) || title_id.empty())) {
		return {};
	}
	if (!std::ranges::all_of(title_id, [](unsigned char c) {
		    return std::isalnum(c) != 0 || c == '-' || c == '_';
	    })) {
		return {};
	}
	return title_id;
}

template <typename... Args>
void PipelineCacheLog(fmt::format_string<Args...> format, Args&&... args) {
	auto message = fmt::format(format, std::forward<Args>(args)...);
	message += '\n';
	Log::WriteToConsoleAndLog(message);
}

bool ReadShaderGuestMemory(void*, uint64_t address, std::span<uint32_t> values) {
	return !values.empty() && Libs::LibKernel::Memory::TryReadGpuCleanBacking(
	                              address, values.data(), values.size_bytes());
}

void DumpShaderSpirv(const char* stage_name, uint64_t shader_hash,
                     const std::vector<uint32_t>& spirv) {
	if (!Config::GraphicsDebugDumpEnabled()) {
		return;
	}
	static std::atomic_int id = 0;
	const auto path = Config::GetShaderLogFolder() / fmt::format("{:04d}_new_shader_{}_{:016x}.spv",
	                                                             id++, stage_name, shader_hash);
	Common::File::CreateDirectories(path.parent_path());
	Common::File file(path);
	if (file.IsInvalid()) {
		const auto path_text = Common::PathToString(path);
		LOGF_COLOR(Log::Color::BrightRed, "Can't create file: %s\n", path_text.c_str());
		return;
	}
	file.Write(spirv.data(), spirv.size() * sizeof(uint32_t));
}

void DumpShaderOriginal(const char* stage_name, uint64_t shader_hash,
                        std::span<const uint32_t> code) {
	if (!Config::GraphicsDebugDumpEnabled()) {
		return;
	}
	EXIT_IF(code.empty());
	static std::atomic_int id = 0;
	const auto             path =
	    Config::GetShaderLogFolder() / "original" /
	    fmt::format("{:04d}_new_shader_{}_{:016x}.bin", id++, stage_name, shader_hash);
	Common::File::CreateDirectories(path.parent_path());
	Common::File file(path);
	if (file.IsInvalid()) {
		const auto path_text = Common::PathToString(path);
		LOGF_COLOR(Log::Color::BrightRed, "Can't create file: %s\n", path_text.c_str());
		return;
	}
	file.Write(code.data(), code.size_bytes());
}

// SPIR-V that already passed validation once (same bytes, same SPIRV-Tools build) is recorded
// on disk, so later launches skip the validator for it. In UFC 5 validation was ~60% of all shader
// build time (~7 ms per shader, ~5.5 s per session), stalling intros and menus. New or changed
// SPIR-V is still validated. KYTY_UFC_SPIRV_VALIDATION_CACHE=0 disables the cache.
class ValidatedSpirvCache {
public:
	static ValidatedSpirvCache& Instance() {
		static ValidatedSpirvCache cache;
		return cache;
	}
	[[nodiscard]] bool Contains(const XXH128_hash_t& hash) {
		std::scoped_lock lock(m_mutex);
		return m_enabled && m_entries.contains(Key(hash));
	}
	void Add(const XXH128_hash_t& hash) {
		std::scoped_lock lock(m_mutex);
		if (!m_enabled || !m_entries.insert(Key(hash)).second || m_file == nullptr) {
			return;
		}
		std::fwrite(&hash, sizeof(hash), 1, m_file);
		std::fflush(m_file);
	}

private:
	static std::pair<uint64_t, uint64_t> Key(const XXH128_hash_t& hash) {
		return {hash.low64, hash.high64};
	}
	struct PairHash {
		size_t operator()(const std::pair<uint64_t, uint64_t>& value) const noexcept {
			return static_cast<size_t>(value.first ^ (value.second * 0x9e3779b97f4a7c15ull));
		}
	};
	ValidatedSpirvCache() {
		const char* setting = std::getenv("KYTY_UFC_SPIRV_VALIDATION_CACHE");
		m_enabled           = setting == nullptr || std::strcmp(setting, "0") != 0;
		if (!m_enabled) {
			return;
		}
		// Header: format tag and the validator build; any mismatch starts a new file.
		const std::string header =
		    fmt::format("KYTY-SPIRV-VALID-1 {} vulkan1.3\n", spvSoftwareVersionDetailsString());
		const auto path = std::filesystem::path("_PipelineCache") / "validated_spirv.bin";
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		bool reuse = false;
		if (std::FILE* in = std::fopen(Common::PathToString(path).c_str(), "rb"); in != nullptr) {
			std::string existing(header.size(), '\0');
			reuse = std::fread(existing.data(), 1, existing.size(), in) == existing.size() &&
			        existing == header;
			XXH128_hash_t hash {};
			while (reuse && std::fread(&hash, sizeof(hash), 1, in) == 1) {
				m_entries.insert(Key(hash));
			}
			std::fclose(in);
		}
		m_file = std::fopen(Common::PathToString(path).c_str(), reuse ? "ab" : "wb");
		if (m_file != nullptr && !reuse) {
			std::fwrite(header.data(), 1, header.size(), m_file);
			std::fflush(m_file);
		}
		std::printf("SPIR-V validation cache: %zu known-valid shaders%s\n", m_entries.size(),
		            m_file == nullptr ? " (read-only)" : "");
	}
	~ValidatedSpirvCache() {
		if (m_file != nullptr) {
			std::fclose(m_file);
		}
	}
	std::mutex                                                     m_mutex;
	std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash>    m_entries;
	std::FILE*                                                     m_file    = nullptr;
	bool                                                           m_enabled = true;
};

bool ValidateShaderSpirv(const char* label, uint64_t shader_hash,
                         const std::vector<uint32_t>& spirv) {
	if (!Config::ShaderValidationEnabled()) {
		return true;
	}
	const auto spirv_hash = XXH3_128bits(spirv.data(), spirv.size() * sizeof(uint32_t));
	auto&      known      = ValidatedSpirvCache::Instance();
	if (known.Contains(spirv_hash)) {
		return true;
	}
	spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
	std::string          messages;
	tools.SetMessageConsumer([&messages](spv_message_level_t, const char*,
	                                     const spv_position_t& position, const char* message) {
		messages += fmt::format("{}: {} ({}) {}\n", static_cast<int>(position.line),
		                        static_cast<int>(position.column), static_cast<int>(position.index),
		                        message);
	});
	if (tools.Validate(spirv)) {
		known.Add(spirv_hash);
		return true;
	}
	spvtools::SpirvTools disassembler(SPV_ENV_VULKAN_1_2);
	std::string          text;
	disassembler.Disassemble(spirv, &text,
	                         static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_COMMENT) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_INDENT) |
	                             static_cast<uint32_t>(SPV_BINARY_TO_TEXT_OPTION_COLOR));
	LOGF_COLOR(Log::Color::BrightRed, "%s SPIR-V validation failed hash=0x%016" PRIx64 ":\n%s",
	           label, shader_hash, messages.c_str());
	LOGF("%s\n", text.c_str());
	return false;
}

} // namespace

std::size_t
PipelineCache::GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey& key) const {
	std::size_t hash = 0;
	PipelineKeyHash::Mix(hash, key.rendering.color_count);
	for (uint32_t i = 0; i < key.rendering.color_count; i++) {
		PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.color_formats[i]));
	}
	PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.depth_format));
	PipelineKeyHash::Mix(hash, static_cast<uint32_t>(key.rendering.stencil_format));
	for (const auto id: key.vertex_shader_ids) {
		PipelineKeyHash::Mix(hash, id);
	}
	PipelineKeyHash::Mix(hash, key.ps_shader_id);
	PipelineKeyHash::Mix(hash, key.vertex_input.binding_count);
	for (uint32_t i = 0; i < key.vertex_input.binding_count; i++) {
		PipelineKeyHash::Mix(hash, key.vertex_input.bindings[i].stride);
		PipelineKeyHash::Mix(hash, key.vertex_input.bindings[i].instance);
	}
	PipelineKeyHash::Mix(hash, key.vertex_input.attribute_count);
	for (uint32_t i = 0; i < key.vertex_input.attribute_count; i++) {
		PipelineKeyHash::Mix(hash, key.vertex_input.attributes[i].offset);
		PipelineKeyHash::Mix(hash, key.vertex_input.attributes[i].binding);
	}
	PipelineKeyHash::Mix(hash, XXH3_64bits(&key.static_params, sizeof(key.static_params)));
	return hash;
}

struct PipelineCache::ProgramCache {
	struct ProgramKey {
		ShaderType            stage           = ShaderType::Unknown;
		uint64_t              hash            = 0;
		uint32_t              user_data_count = 0;
		uint32_t              code_size       = 0;
		std::vector<uint32_t> static_state;

		bool operator==(const ProgramKey&) const = default;
	};

	struct Permutation {
		ShaderRecompiler::IR::ResourceSpecialization specialization;
		ShaderRecompiler::IR::CompiledShaderInfo     program;
		ShaderProgram                                handle;
	};

	// One cached materialization of a source (KYTY_UFC_SRT_MEMO). The `reads` log is the
	// invalidation proof: ReplaySrtReads proves the guest-memory reads are unchanged, and the
	// user-data mask proves the read SGPRs are unchanged, so `resources`/`specialization` are
	// exactly what a fresh MaterializeResources would produce for those inputs.
	struct MemoSlot {
		bool                                         valid       = false;
		uint64_t                                     shader_base = 0;
		std::vector<uint32_t>                        user_data;
		ShaderRecompiler::IR::SrtReadLog             reads;
		ShaderRecompiler::IR::ResourceSnapshot       resources;
		ShaderRecompiler::IR::ResourceSpecialization specialization;
	};

	struct SourceEntry {
		explicit SourceEntry(ShaderRecompiler::IR::ResourcePlan plan)
		    : resource_plan(std::move(plan)) {
			permutations.reserve(8);
		}

		ShaderRecompiler::IR::ResourcePlan resource_plan;
		std::vector<Permutation>           permutations;
		// Bounded per-source memo of materializations. Only slot 0 is used unless
		// KYTY_UFC_SRT_MEMO_SLOTS raises the count; more slots let a source whose draws interleave
		// several (shader base, user data) keys reuse an older materialization instead of only the
		// immediately preceding one.
		std::vector<MemoSlot> memos;
		uint32_t              next_slot = 0;
		ShaderDiskCache::Bytes disk_key;
	};

	static bool DiskAllowed() {
		static const bool enabled = [] {
			const char* value = std::getenv("KYTY_SHADER_DISK_CACHE");
			return value == nullptr || std::strcmp(value, "0") != 0;
		}();
		// Traced shaders embed this process's diagnostic address and reserve push word 31.
		return enabled && !ShaderRecompiler::NanTrace::Active() && !Config::GraphicsDebugDumpEnabled();
	}
	void EnsureDisk() {
		if (disk || !DiskAllowed()) return;
		const auto title = PipelineCacheTitleId();
		if (title.empty()) return;
		disk = std::make_unique<ShaderDiskCache::Store>(std::filesystem::path("_ShaderCache") / title);
		PipelineCacheLog("Shader disk cache: _ShaderCache/{} (source {})", title, ShaderDiskCache::BuildFingerprint());
	}
	ShaderDiskCache::Bytes SourceDiskKey(const ShaderParams& params) {
		ShaderDiskCache::ByteWriter key;
		ShaderDiskCache::AppendSession(key);
		key.Put<uint8_t>('S');
		key.Put(properties.vendorID); key.Put(properties.deviceID); key.Put(properties.driverVersion);
		for (auto byte: properties.pipelineCacheUUID) key.Put(byte);
		key.Put(Config::GetShaderOptimizationType());
		key.Put(lookup_key.stage); key.Put(params.hash); key.Put(params.user_data_count);
		key.Words(params.code); key.Words(params.back_code); key.Words(lookup_key.static_state);
		return std::move(key.bytes);
	}
	ShaderDiskCache::Bytes PermutationDiskKey(const SourceEntry& source,
	                                         const ShaderRecompiler::IR::ResourceSpecialization& specialization,
	                                         uint32_t push_cursor) {
		ShaderDiskCache::ByteWriter key;
		key.Put<uint8_t>('P'); key.Put<uint64_t>(source.disk_key.size()); key.PutBytes(source.disk_key);
		ShaderDiskCache::AppendSpecialization(key, specialization); key.Put(push_cursor);
		return std::move(key.bytes);
	}
	void SaveSource(const ShaderDiskCache::Bytes& key, const ShaderRecompiler::IR::ResourcePlan& plan) {
		if (!disk || !DiskAllowed() || key.empty()) return;
		ShaderDiskCache::Bytes payload, checked;
		ShaderRecompiler::IR::ResourcePlan decoded;
		if (ShaderDiskCache::EncodeSource(plan, payload) && ShaderDiskCache::DecodeSource(payload, decoded) &&
		    ShaderDiskCache::EncodeSource(decoded, checked) && payload == checked)
			disk->Save(key, std::move(payload));
	}
	void FlushDisk() {
		if (!disk) return;
		disk->Flush();
		PipelineCacheLog("Shader disk cache: source hits={} permutation hits={} misses={} rejected={} writes={} dropped={}",
		                 source_hits, permutation_hits, disk->counters.misses, disk->counters.rejected,
		                 disk->counters.queued, disk->counters.dropped);
	}

	// KYTY_UFC_SRT_MEMO: unset/0 off, 1 reuse the previous materialization when its inputs and
	// every guest-memory read are unchanged, "shadow" also recompute and count mismatches.
	static int SrtMemoMode() {
		static const int mode = [] {
			const char* value = std::getenv("KYTY_UFC_SRT_MEMO");
			if (value == nullptr) return 1; // Dev 094 all-on bisect: on by default, =0 turns it off (shadow = verify)
			if (std::strcmp(value, "1") == 0) return 1;
			if (std::strcmp(value, "shadow") == 0) return 2;
			return 0;
		}();
		return mode;
	}

	// KYTY_UFC_SRT_MEMO_SLOTS: per-source memo capacity, clamped to [1, 64]; default 1 keeps the
	// original single-slot behavior. Only meaningful while SrtMemoMode() != 0.
	static size_t SrtMemoSlots() {
		static const size_t slots = [] {
			const char* value = std::getenv("KYTY_UFC_SRT_MEMO_SLOTS");
			if (value == nullptr) return size_t {1};
			const long parsed = std::strtol(value, nullptr, 10);
			if (parsed < 1) return size_t {1};
			if (parsed > 64) return size_t {64};
			return static_cast<size_t>(parsed);
		}();
		return slots;
	}

	static bool SameSnapshot(const ShaderRecompiler::IR::ResourceSnapshot& a,
	                         const ShaderRecompiler::IR::ResourceSnapshot& b) {
		return a.buffers == b.buffers && a.images == b.images && a.samplers == b.samplers &&
		       a.flattened_srt == b.flattened_srt && a.user_data == b.user_data &&
		       a.specialization_reads == b.specialization_reads && a.uniform_fill == b.uniform_fill;
	}

	// True when the memo slot was materialized with the same shader base and the same user-data
	// words that its own evaluation read. Unread user-data words reach the result only through
	// ResourceSnapshot::user_data, which the caller copies in.
	static bool MemoInputsMatch(const MemoSlot&                              slot,
	                            const ShaderRecompiler::IR::SrtRuntime&      runtime) {
		if (!slot.valid || slot.shader_base != runtime.shader_base ||
		    slot.user_data.size() != runtime.user_data.size()) {
			return false;
		}
		const auto mask = slot.reads.user_data_mask;
		if ((mask >> 63u) != 0u) {
			return std::ranges::equal(slot.user_data, runtime.user_data);
		}
		for (size_t i = 0; i < slot.user_data.size() && i < 63; ++i) {
			if (((mask >> i) & 1u) != 0u && slot.user_data[i] != runtime.user_data[i]) {
				return false;
			}
		}
		return true;
	}

	static void MaterializeSlot(SourceEntry& source, MemoSlot& slot,
	                            const ShaderRecompiler::IR::SrtRuntime& runtime,
	                            ShaderRecompiler::IR::SrtReadLog*       log) {
		slot.valid = false;
		EXIT_IF(!ShaderRecompiler::IR::MaterializeResources(source.resource_plan, runtime,
		                                                    slot.resources, slot.specialization,
		                                                    log));
		if (ShaderRecompiler::IR::SrtNativeStats stats;
		    ShaderRecompiler::IR::TakeSrtNativeReport(stats)) {
			PipelineCacheLog("SRT native: {} plans, {} failed, {} KiB, {} instructions, {} interpreted",
			                 stats.plans, stats.failed, stats.bytes / 1024u,
			                 stats.instructions, stats.interpreted);
		}
		slot.shader_base = runtime.shader_base;
		slot.user_data.assign(runtime.user_data.begin(), runtime.user_data.end());
		slot.valid = true;
	}

	// Returns the memo slot holding the materialization for this runtime. The caller must keep the
	// source alive and must not call this again for the same source before consuming the result.
	static MemoSlot& MaterializeSource(SourceEntry& source, const ShaderRecompiler::IR::SrtRuntime& runtime,
	                                   uint64_t shader_hash) {
		if (source.memos.empty()) {
			source.memos.resize(SrtMemoSlots());
		}
		auto&     memos = source.memos;
		const int mode  = SrtMemoMode();
		if (mode == 0) {
			MaterializeSlot(source, memos[0], runtime, nullptr);
			return memos[0];
		}
		MemoSlot* hit          = nullptr;
		bool      inputs_found = false;
		for (auto& slot: memos) {
			if (!MemoInputsMatch(slot, runtime)) {
				continue;
			}
			inputs_found      = true;
			uint64_t replayed = 0;
			if (ShaderRecompiler::IR::ReplaySrtReads(runtime, slot.reads, replayed)) {
				WaitStats::NoteSrtMemo(4, replayed);
				hit = &slot;
				break;
			}
		}
		if (hit != nullptr) {
			WaitStats::NoteSrtMemo(0);
			if (mode == 1) {
				// Unread user-data words only reach the result through this copy.
				hit->resources.user_data.assign(runtime.user_data.begin(), runtime.user_data.end());
				return *hit;
			}
			// Shadow mode: recompute and compare, keeping the freshly computed result in the slot.
			auto shadow_resources = hit->resources;
			shadow_resources.user_data.assign(runtime.user_data.begin(), runtime.user_data.end());
			auto shadow_specialization = hit->specialization;
			auto shadow_active         = source.resource_plan.active_sources;
			MaterializeSlot(source, *hit, runtime, &hit->reads);
			if (!(SameSnapshot(shadow_resources, hit->resources) &&
			      shadow_specialization == hit->specialization &&
			      shadow_active == source.resource_plan.active_sources)) {
				WaitStats::NoteSrtMemo(3);
				static std::atomic_uint reported {0};
				if (reported.fetch_add(1, std::memory_order_relaxed) < 16) {
					PipelineCacheLog("SRT memo shadow MISMATCH: shader hash={:016x} reads={}", shader_hash,
					                 hit->reads.records.size());
				}
			}
			return *hit;
		}
		WaitStats::NoteSrtMemo(inputs_found ? 2 : 1);
		MemoSlot& slot   = memos[source.next_slot];
		source.next_slot = (source.next_slot + 1u) % static_cast<uint32_t>(memos.size());
		MaterializeSlot(source, slot, runtime, &slot.reads);
		return slot;
	}

	struct ProgramKeyHash {
		std::size_t operator()(const ProgramKey& key) const {
			std::size_t hash = static_cast<std::size_t>(key.stage);
			PipelineKeyHash::Mix(hash, static_cast<std::size_t>(key.hash));
			if constexpr (sizeof(std::size_t) < sizeof(uint64_t)) {
				PipelineKeyHash::Mix(hash, static_cast<std::size_t>(key.hash >> 32u));
			}
			PipelineKeyHash::Mix(hash, key.user_data_count);
			PipelineKeyHash::Mix(hash, key.code_size);
			PipelineKeyHash::Mix(hash, key.static_state.size());
			// Bucket same-shape static variants by source. ProgramKey equality performs the one
			// exact state comparison needed on a stable hit without hashing the full state first.
			return hash;
		}
	};

	static constexpr std::size_t MaxStaticKeyWords = 32 + ShaderVertexInputInfo::RES_MAX * 6;

	Permutation CompilePermutation(const char*                                  stage_name,
	                               const ShaderRecompiler::CompileOptions&      options,
	                               ShaderRecompiler::TranslateResult            translated,
	                               ShaderRecompiler::IR::ResourceSpecialization specialization,
	                               uint32_t push_data_start_dword,
	                               const ShaderDiskCache::Bytes& disk_key = {}) {
		const auto compile_begin = SteadyNs();
		auto result = ShaderRecompiler::CompileProgram(std::move(translated), options,
		                                               specialization, push_data_start_dword);
		const auto validate_begin = SteadyNs();
		BuildTimes().compile_ns.fetch_add(validate_begin - compile_begin, std::memory_order_relaxed);
		const bool valid = ValidateShaderSpirv(options.dump_label, options.shader_hash, result.spirv);
		const auto module_begin = SteadyNs();
		BuildTimes().validate_ns.fetch_add(module_begin - validate_begin, std::memory_order_relaxed);
		if (!valid) {
			DumpShaderSpirv(stage_name, options.shader_hash, result.spirv);
			EXIT("%s failed hash=0x%016" PRIx64 ": SPIR-V validation failed\n", options.dump_label,
			     options.shader_hash);
		}
		DumpShaderSpirv(stage_name, options.shader_hash, result.spirv);
		auto program = std::move(result.program).TakeCompiledInfo();
		if (disk && DiskAllowed() && !disk_key.empty()) {
			ShaderDiskCache::Bytes payload, checked;
			std::vector<uint32_t> decoded_spirv;
			ShaderRecompiler::IR::CompiledShaderInfo decoded;
			if (ShaderDiskCache::EncodePermutation(result.spirv, program, payload) &&
			    ShaderDiskCache::DecodePermutation(payload, decoded_spirv, decoded) &&
			    ShaderDiskCache::EncodePermutation(decoded_spirv, decoded, checked) && payload == checked)
				disk->Save(disk_key, std::move(payload));
		}

		const auto module = CompileSPV(result.spirv, device);
		BuildTimes().module_ns.fetch_add(SteadyNs() - module_begin, std::memory_order_relaxed);
		EXIT_IF(module == nullptr);
		if (options.dump_ir) {
			LOGF("%s SPIR-V words=%" PRIu64 " wave_size=%u\n", options.dump_label,
			     static_cast<uint64_t>(result.spirv.size()), options.wave_size);
		}
		return {
		    .specialization = std::move(specialization),
		    .program        = std::move(program),
		    .handle         = {.id = ++next_shader_id, .module = module},
		};
	}

	template <typename InputInfo>
	ShaderProgram Get(const ShaderParams& params, InputInfo& input_info,
	                  uint32_t& push_data_cursor) {
		ShaderType stage;
		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			stage = input_info.logical_stage;
		} else if constexpr (std::is_same_v<InputInfo, ShaderPixelInputInfo>) {
			stage = ShaderType::Pixel;
		} else {
			static_assert(std::is_same_v<InputInfo, ShaderComputeInputInfo>);
			stage = ShaderType::Compute;
		}

		const auto user_data       = std::span(params.user_data).first(params.user_data_count);
		lookup_key.stage           = stage;
		lookup_key.hash            = params.hash;
		lookup_key.user_data_count = params.user_data_count;
		lookup_key.code_size       = static_cast<uint32_t>(params.code.size());
		typename std::unordered_map<ProgramKey, SourceEntry, ProgramKeyHash>::iterator entry;
		{
			KYTY_PROFILER_BLOCK("Shader::BuildKeyAndFind");
			BuildStageStaticKey(input_info, lookup_key.static_state);
			entry = programs.find(lookup_key);
		}
		const ShaderRecompiler::IR::SrtRuntime runtime {
		    .user_data                  = user_data,
		    .shader_base                = params.Base(),
		    .read_specialization_memory = ReadShaderGuestMemory,
		};
		ShaderDiskCache::Bytes source_disk_key;
		if (entry == programs.end() && DiskAllowed()) {
			EnsureDisk();
			if (disk) {
				source_disk_key = SourceDiskKey(params);
				ShaderDiskCache::Bytes payload;
				ShaderRecompiler::IR::ResourcePlan plan;
				if (disk->Load(source_disk_key, payload)) {
					if (ShaderDiskCache::DecodeSource(payload, plan) && plan.stage == stage &&
					    plan.shader_hash == params.hash && plan.user_data_count == params.user_data_count) {
						entry = programs.try_emplace(lookup_key, std::move(plan)).first;
						entry->second.disk_key = source_disk_key;
						source_hits++;
					} else disk->counters.rejected++;
				}
			}
		}
		MemoSlot* slot = nullptr;
		if (entry != programs.end()) {
			{
				KYTY_PROFILER_BLOCK("Shader::MaterializeResources");
				const ScopedSrtEvaluation srt_evaluation;
				slot = &MaterializeSource(entry->second, runtime, params.hash);
			}
			KYTY_PROFILER_BLOCK("Shader::FindPermutation");
			if (const auto permutation = std::ranges::find_if(
			        entry->second.permutations,
			        [&](const Permutation& candidate) {
				        const auto& layout = candidate.program.bindings;
				        return layout.push_data_start_dword ==
				                   ShaderRecompiler::IR::PushData::StartFor(
				                       push_data_cursor, layout.ShaderDataDwords(),
				                       ShaderRecompiler::NanTrace::Active()
				                           ? ShaderRecompiler::NanTrace::EventPushDword
				                           : ShaderRecompiler::IR::PushData::DwordCount) &&
				               candidate.specialization == slot->specialization;
			        });
			    permutation != entry->second.permutations.end()) {
				input_info.stage = {.program   = &permutation->program,
				                    .resources = &slot->resources};
				permutation->program.bindings.AdvancePushData(push_data_cursor);
				return permutation->handle;
			}
		}

		ShaderStageInputInfo stage_input {};
		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			stage_input.vertex = &input_info;
		} else if constexpr (std::is_same_v<InputInfo, ShaderPixelInputInfo>) {
			stage_input.pixel = &input_info;
		} else {
			stage_input.compute = &input_info;
		}
		const char* label      = nullptr;
		const char* stage_name = nullptr;
		switch (stage) {
			case ShaderType::Vertex:
				label      = "ShaderRecompiler VS";
				stage_name = "vs";
				break;
			case ShaderType::Mesh:
				label      = "ShaderRecompiler MS";
				stage_name = "ms";
				break;
			case ShaderType::Local:
				label      = "ShaderRecompiler LS";
				stage_name = "ls";
				break;
			case ShaderType::TessellationControl:
				label      = "ShaderRecompiler HS";
				stage_name = "hs";
				break;
			case ShaderType::TessellationEvaluation:
				label      = "ShaderRecompiler DS";
				stage_name = "ds";
				break;
			case ShaderType::Pixel:
				label      = "ShaderRecompiler PS";
				stage_name = "ps";
				break;
			case ShaderType::Compute:
				label      = "ShaderRecompiler CS";
				stage_name = "cs";
				break;
			default: EXIT("invalid pipeline shader stage\n");
		}
		ShaderRecompiler::CompileOptions options;
		options.stage       = stage;
		options.shader_hash = params.hash;
		options.user_data   = user_data;
		options.back_code   = params.back_code;
		options.dump_ir     = Config::GetShaderLogDirection() != Config::LogDirection::Silent;
		options.early_dump  = options.dump_ir;
		options.dump_label  = label;
		options.input_info  = stage_input;

		if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
			options.user_data_base = 8;
			options.wave_size      = input_info.wave_size;
			if (stage == ShaderType::Mesh || stage == ShaderType::TessellationControl) {
				options.user_data_base = 0;
				options.wave_size = stage == ShaderType::Mesh ? input_info.mesh.wave_size : 64u;
			}
		} else {
			options.wave_size = input_info.wave_size;
		}
		DumpShaderOriginal(stage_name, options.shader_hash, params.code);
		ShaderDiskCache::Bytes permutation_disk_key;
		if (DiskAllowed()) {
			EnsureDisk();
			if (disk && entry != programs.end()) {
				if (entry->second.disk_key.empty()) entry->second.disk_key = SourceDiskKey(params);
				permutation_disk_key = PermutationDiskKey(entry->second, slot->specialization, push_data_cursor);
				ShaderDiskCache::Bytes payload;
				std::vector<uint32_t> spirv;
				ShaderRecompiler::IR::CompiledShaderInfo program;
				if (disk->Load(permutation_disk_key, payload)) {
					if (ShaderDiskCache::DecodePermutation(payload, spirv, program) &&
					    program.stage == stage && program.shader_hash == params.hash &&
					    program.wave_size == options.wave_size && program.user_data_base == options.user_data_base &&
					    program.user_data_count == params.user_data_count &&
					    program.bindings.push_data_start_dword == ShaderRecompiler::IR::PushData::StartFor(
					        push_data_cursor, program.bindings.ShaderDataDwords()) &&
					    ValidateShaderSpirv(label, params.hash, spirv)) {
						const auto module = CompileSPV(spirv, device);
						EXIT_IF(module == nullptr);
						entry->second.permutations.push_back({slot->specialization, std::move(program),
						    {.id = ++next_shader_id, .module = module}});
						const auto& cached = entry->second.permutations.back();
						input_info.stage = {.program = &cached.program, .resources = &slot->resources};
						cached.program.bindings.AdvancePushData(push_data_cursor);
						if (++permutation_hits <= 8 || permutation_hits % 128 == 0)
							PipelineCacheLog("Shader disk cache hit: stage={} hash={:016x} total={}",
							                 stage_name, params.hash, permutation_hits);
						return cached.handle;
					}
					disk->counters.rejected++;
				}
			}
		}
		const auto translate_begin = SteadyNs();
		auto translated = [&] {
			KYTY_PROFILER_BLOCK("Shader::TranslateProgram");
			return ShaderRecompiler::TranslateProgram(params.code, options);
		}();
		BuildTimes().translate_ns.fetch_add(SteadyNs() - translate_begin,
		                                    std::memory_order_relaxed);
		if (entry == programs.end()) {
			// Load-window attribution (Dev 118): time the un-zoned ResourcePlan extraction.
			ShaderRecompiler::IR::ResourcePlan plan;
			{
				KYTY_PROFILER_BLOCK("Shader::ExtractPlan");
				plan = ShaderRecompiler::IR::ExtractResourcePlan(translated.program);
			}
			entry = programs.try_emplace(lookup_key, std::move(plan)).first;
			if (disk && DiskAllowed()) {
				entry->second.disk_key = source_disk_key.empty() ? SourceDiskKey(params) : std::move(source_disk_key);
				SaveSource(entry->second.disk_key, entry->second.resource_plan);
			}
			slot  = &MaterializeSource(entry->second, runtime, params.hash);
		}
		if (disk && DiskAllowed() && permutation_disk_key.empty())
			permutation_disk_key = PermutationDiskKey(entry->second, slot->specialization, push_data_cursor);
		{
			KYTY_PROFILER_BLOCK("Shader::CompilePermutation");
			entry->second.permutations.push_back(
			    CompilePermutation(stage_name, options, std::move(translated),
			                       slot->specialization, push_data_cursor, permutation_disk_key));
		}
		const auto& permutation       = entry->second.permutations.back();
		const auto  permutation_count = entry->second.permutations.size();
		if (Config::ProfilerEnabled() &&
		    (permutation_count == 1 || (permutation_count & (permutation_count - 1)) == 0)) {
			const auto diagnostic = fmt::format(
			    "Shader cache: stage={} hash={:016x} permutations_for_key={} source_keys={}\n",
			    stage_name, options.shader_hash, permutation_count, programs.size());
			std::printf("%s", diagnostic.c_str());
			if constexpr (std::is_same_v<InputInfo, ShaderVertexInputInfo>) {
				if (permutation_count == 1) {
					const auto state = fmt::format(
					    "Shader VS state: hash={:016x} userdata={} code_words={} key={:08x} "
					    "clip={} scale=({},{}) offset=({},{}) extent=({},{})\n",
					    options.shader_hash, lookup_key.user_data_count, lookup_key.code_size,
					    fmt::join(lookup_key.static_state, ","), input_info.clip_space.enabled,
					    input_info.clip_space.scale[0], input_info.clip_space.scale[1],
					    input_info.clip_space.offset[0], input_info.clip_space.offset[1],
					    input_info.clip_space.half_extent[0], input_info.clip_space.half_extent[1]);
					std::printf("%s", state.c_str());
				}
			}
		}
		input_info.stage = {.program = &permutation.program, .resources = &slot->resources};
		permutation.program.bindings.AdvancePushData(push_data_cursor);

		// Load-window attribution (Dev 118): the retained build-stats print walks every program and
		// writes to the (possibly pipe-backed) console on the GPU command thread. Time it so a
		// multi-second parent stall can be attributed to it rather than to shader work.
		{
			KYTY_PROFILER_BLOCK("Shader::BuildStatsPrintf");
			std::array<size_t, static_cast<size_t>(ShaderType::TessellationEvaluation) + 1> counts {};
			for (const auto& [key, source]: programs) {
				counts[static_cast<size_t>(key.stage)] += source.permutations.size();
			}
			// Guest geometry shaders are compiled through the host mesh stage.
			auto& times = BuildTimes();
			std::printf("Shader build ms: translate %.0f compile %.0f validate %.0f module %.0f | "
			            "pipelines %llu in %.0f ms\n",
			            static_cast<double>(times.translate_ns.load()) / 1e6,
			            static_cast<double>(times.compile_ns.load()) / 1e6,
			            static_cast<double>(times.validate_ns.load()) / 1e6,
			            static_cast<double>(times.module_ns.load()) / 1e6,
			            static_cast<unsigned long long>(times.pipelines.load()),
			            static_cast<double>(times.pipeline_ns.load()) / 1e6);
			std::printf("Shaders: VS %zu | PS %zu | CS %zu | GS %zu | LS %zu | HS %zu | TES %zu\n",
			            counts[static_cast<size_t>(ShaderType::Vertex)],
			            counts[static_cast<size_t>(ShaderType::Pixel)],
			            counts[static_cast<size_t>(ShaderType::Compute)],
			            counts[static_cast<size_t>(ShaderType::Mesh)],
			            counts[static_cast<size_t>(ShaderType::Local)],
			            counts[static_cast<size_t>(ShaderType::TessellationControl)],
			            counts[static_cast<size_t>(ShaderType::TessellationEvaluation)]);
		}
		return permutation.handle;
	}

	explicit ProgramCache(vk::Device device, vk::PhysicalDeviceProperties properties): device(device), properties(properties) {
		lookup_key.static_state.reserve(MaxStaticKeyWords);
	}
	~ProgramCache() {
		for (const auto& [key, entry]: programs) {
			(void)key;
			for (const auto& permutation: entry.permutations) {
				device.destroyShaderModule(permutation.handle.module, nullptr);
			}
		}
	}

	std::unordered_map<ProgramKey, SourceEntry, ProgramKeyHash> programs;
	ProgramKey                                                  lookup_key;
	vk::Device                                                  device;
	vk::PhysicalDeviceProperties                                properties;
	std::unique_ptr<ShaderDiskCache::Store>                      disk;
	uint64_t                                                    source_hits = 0, permutation_hits = 0;
	uint64_t                                                    next_shader_id = 0;
};

PipelineCache::PipelineCache(GraphicContext& graphics)
    : m_graphics(graphics), m_program_cache(std::make_unique<ProgramCache>(graphics.device, graphics.GetPhysicalDeviceProperties())) {
	EXIT_NOT_IMPLEMENTED(!Common::Thread::IsMainThread());
	InitializeDriverCache();
}

PipelineCache::~PipelineCache() {
	Save();
	auto destroy = [this](const auto& pipelines) {
		for (const auto& [key, pipeline]: pipelines) {
			(void)key;
			m_graphics.device.destroyPipeline(pipeline->pipeline, nullptr);
			m_graphics.device.destroyPipelineLayout(pipeline->pipeline_layout, nullptr);
			m_graphics.device.destroyDescriptorSetLayout(pipeline->descriptor_set_layout, nullptr);
		}
	};
	destroy(m_graphics_pipelines);
	destroy(m_compute_pipelines);
	if (m_driver_cache != nullptr) {
		m_graphics.device.destroyPipelineCache(m_driver_cache, nullptr);
	}
}

void PipelineCache::InitializeDriverCache() {
	const auto title_id = PipelineCacheTitleId();
	if (title_id.empty()) {
		return;
	}
	if (KYTY_BUILD != KYTY_BUILD_RELEASE) {
		PipelineCacheLog("Vulkan pipeline cache: disabled (non-Release build)");
		return;
	}

	m_driver_cache_path     = std::filesystem::path("_PipelineCache") / (title_id + ".bin");
	const auto path         = Common::PathToString(m_driver_cache_path);
	const bool cache_exists = Common::File::IsFileExisting(m_driver_cache_path);
	if (cache_exists) {
		PipelineCacheLog("Vulkan pipeline cache: loading {}", path);
	} else {
		PipelineCacheLog("Vulkan pipeline cache: initializing {}", path);
	}
	std::vector<uint8_t> initial_data;
	if (cache_exists) {
		Common::File file(m_driver_cache_path, Common::File::Mode::Read);
		const auto   file_size = file.IsInvalid() ? 0 : file.Size();
		const auto   signature = DriverCacheSignature(m_graphics.GetPhysicalDeviceProperties());
		if (file_size >= signature.size() + sizeof(uint64_t) &&
		    file_size <= std::numeric_limits<uint32_t>::max()) {
			std::string cached_signature(signature.size(), '\0');
			uint64_t    payload_hash = 0;
			initial_data.resize(file_size - signature.size() - sizeof(payload_hash));
			uint32_t signature_read = 0;
			uint32_t hash_read      = 0;
			uint32_t payload_read   = 0;
			file.Read(cached_signature.data(), static_cast<uint32_t>(cached_signature.size()),
			          &signature_read);
			file.Read(&payload_hash, sizeof(payload_hash), &hash_read);
			file.Read(initial_data.data(), static_cast<uint32_t>(initial_data.size()),
			          &payload_read);
			file.Close();
			if (signature_read != cached_signature.size() || hash_read != sizeof(payload_hash) ||
			    payload_read != initial_data.size() || cached_signature != signature ||
			    XXH3_64bits(initial_data.data(), initial_data.size()) != payload_hash) {
				initial_data.clear();
				PipelineCacheLog(
				    "Vulkan pipeline cache: invalidating {} (driver, emulator, or data mismatch)",
				    path);
			}
		} else {
			file.Close();
			PipelineCacheLog("Vulkan pipeline cache: invalidating {} (invalid file size)", path);
		}
	}

	vk::PipelineCacheCreateInfo create {};
	create.initialDataSize = initial_data.size();
	create.pInitialData    = initial_data.empty() ? nullptr : initial_data.data();
	auto result = m_graphics.device.createPipelineCache(&create, nullptr, &m_driver_cache);
	if (result != vk::Result::eSuccess && !initial_data.empty()) {
		PipelineCacheLog("Vulkan pipeline cache: driver rejected {} ({}); starting empty", path,
		                 vk::to_string(result));
		initial_data.clear();
		create.initialDataSize = 0;
		create.pInitialData    = nullptr;
		result = m_graphics.device.createPipelineCache(&create, nullptr, &m_driver_cache);
	}
	if (result != vk::Result::eSuccess) {
		PipelineCacheLog("Vulkan pipeline cache: disabled ({})", vk::to_string(result));
		m_driver_cache = nullptr;
		return;
	}
	if (!initial_data.empty()) {
		PipelineCacheLog("Vulkan pipeline cache: loaded {} bytes from {}", initial_data.size(),
		                 path);
	} else {
		PipelineCacheLog("Vulkan pipeline cache: initialized empty");
	}
}

void NotePipelineBuildNs(uint64_t nanoseconds) noexcept {
	BuildTimes().pipeline_ns.fetch_add(nanoseconds, std::memory_order_relaxed);
	BuildTimes().pipelines.fetch_add(1, std::memory_order_relaxed);
}

void PipelineCache::Save() {
	m_program_cache->FlushDisk();
	if (m_driver_cache == nullptr) {
		return;
	}

	size_t               size = 0;
	vk::Result           result;
	std::vector<uint8_t> payload;
	for (uint32_t attempt = 0; attempt < 3; attempt++) {
		size   = 0;
		result = m_graphics.device.getPipelineCacheData(m_driver_cache, &size, nullptr);
		if (result != vk::Result::eSuccess || size == 0 ||
		    size > std::numeric_limits<uint32_t>::max()) {
			break;
		}
		payload.resize(size);
		result = m_graphics.device.getPipelineCacheData(m_driver_cache, &size, payload.data());
		if (result != vk::Result::eIncomplete) {
			break;
		}
	}
	if (result != vk::Result::eSuccess || size == 0 ||
	    size > std::numeric_limits<uint32_t>::max()) {
		PipelineCacheLog("Vulkan pipeline cache: save failed ({}, {} bytes)", vk::to_string(result),
		                 size);
		return;
	}
	payload.resize(size);
	auto       prefix       = DriverCacheSignature(m_graphics.GetPhysicalDeviceProperties());
	const auto payload_hash = XXH3_64bits(payload.data(), payload.size());
	prefix.append(reinterpret_cast<const char*>(&payload_hash), sizeof(payload_hash));
	if (!Common::File::CreateDirectories(m_driver_cache_path.parent_path())) {
		PipelineCacheLog("Vulkan pipeline cache: failed to create cache directory");
		return;
	}
	auto temp_path = m_driver_cache_path;
	temp_path += ".tmp";
	Common::File file;
	uint32_t     prefix_written  = 0;
	uint32_t     payload_written = 0;
	if (file.Create(temp_path)) {
		file.Write(prefix.data(), static_cast<uint32_t>(prefix.size()), &prefix_written);
		file.Write(payload.data(), static_cast<uint32_t>(payload.size()), &payload_written);
	}
	const bool flushed = !file.IsInvalid() && file.Flush();
	file.Close();
	if (prefix_written != prefix.size() || payload_written != payload.size() || !flushed ||
	    !Common::File::RenameFile(temp_path, m_driver_cache_path)) {
		PipelineCacheLog("Vulkan pipeline cache: failed to write {}",
		                 Common::PathToString(m_driver_cache_path));
		return;
	}
	PipelineCacheLog("Vulkan pipeline cache: saved {} bytes to {}", payload.size(),
	                 Common::PathToString(m_driver_cache_path));
	m_graphics.device.destroyPipelineCache(m_driver_cache, nullptr);
	m_driver_cache = nullptr;
}

PipelineCache::GraphicsPrograms PipelineCache::GetGraphicsPrograms(
    const HW::VertexShaderInfo& vertex_regs, const HW::PixelShaderInfo& pixel_regs,
    const HW::ShaderRegisters& sh, const HW::Context& context, const HW::UserConfig& user_config,
    std::span<const Prospero::ColorComponentMapping, 8> target_export_mapping, bool pixel_active,
    std::array<ShaderVertexInputInfo, 3>& vertex_info, ShaderPixelInputInfo& pixel_info) {
	const bool tess_active = user_config.GetPrimType() == Prospero::PrimitiveType::kPatch;
	std::array<ShaderParams, 3> vertex_params;
	if (tess_active) {
		vertex_params = PrepareTessellationPrograms(vertex_regs, context, vertex_info);
	} else {
		vertex_params[0] = PrepareProgram(vertex_regs, context, user_config, vertex_info[0]);
	}
	const bool mesh_active = vertex_info[0].logical_stage == ShaderType::Mesh;
	if (mesh_active) {
		EXIT_NOT_IMPLEMENTED(!m_graphics.mesh_shader_enabled);
		auto& mesh              = vertex_info[0].mesh;
		mesh.host_subgroup_size = m_graphics.subgroup_size;
		const auto& limits      = m_graphics.mesh_shader_properties;
		const auto  logical_threads =
		    mesh.threads_num[0] * mesh.threads_num[1] * mesh.threads_num[2];
		const auto host_threads = ((logical_threads + mesh.wave_size - 1u) / mesh.wave_size) *
		                          std::min(mesh.host_subgroup_size, mesh.wave_size);
		if (host_threads > limits.maxMeshWorkGroupInvocations ||
		    host_threads > limits.maxMeshWorkGroupSize[0] ||
		    mesh.max_vertices > limits.maxMeshOutputVertices ||
		    mesh.max_primitives > limits.maxMeshOutputPrimitives ||
		    mesh.lds_size_dwords * sizeof(uint32_t) > limits.maxMeshSharedMemorySize) {
			EXIT("mesh shader exceeds host limits: threads=%u vertices=%u primitives=%u LDS=%u\n",
			     host_threads, mesh.max_vertices, mesh.max_primitives, mesh.lds_size_dwords);
		}
	}
	ShaderParams pixel_params;
	if (pixel_active) {
		pixel_params      = PrepareProgram(pixel_regs, sh, target_export_mapping, pixel_info);
		const auto& blend = context.GetBlendControl(0);
		pixel_info.dual_source_blending =
		    blend.enable && !context.GetRenderTarget(0).info.blend_bypass &&
		    (BlendFactorIsDualSource(blend.color_srcblend) ||
		     BlendFactorIsDualSource(blend.color_destblend) ||
		     (blend.separate_alpha_blend && (BlendFactorIsDualSource(blend.alpha_srcblend) ||
		                                     BlendFactorIsDualSource(blend.alpha_destblend))));
		if (pixel_info.dual_source_blending) {
			// MRT1 supplies the second blend source for target 0.
			pixel_info.target_output_mode[1]    = pixel_info.target_output_mode[0];
			pixel_info.target_export_mapping[1] = pixel_info.target_export_mapping[0];
		} else if (blend.enable && !context.GetRenderTarget(0).info.blend_bypass &&
		           pixel_info.target_output_mode[0] != 0 && pixel_info.target_output_mode[0] != 7 &&
		           std::all_of(std::begin(pixel_info.target_output_mode) + 1,
		                       std::end(pixel_info.target_output_mode),
		                       [](uint8_t mode) { return mode == 0; }) &&
		           ClassifyBlendMapping(blend, pixel_info.target_export_mapping[0]) ==
		               BlendMappingSupport::SourceAlpha) {
			// Preserve logical alpha when the export mapping moves it.
			pixel_info.alpha_blend_source_remap = true;
			pixel_info.dual_source_blending     = true;
			pixel_info.target_output_mode[1]    = pixel_info.target_output_mode[0];
			pixel_info.target_export_mapping[1] = {};
		}
	}
	if (context.GetClipControl().clip_disable) {
		const auto& viewport = context.GetScreenViewport().viewports[0];
		const auto& limits   = m_graphics.GetPhysicalDeviceProperties().limits;
		auto&       clip     = vertex_info[tess_active ? 2u : 0u].clip_space;
		clip.scale[0]        = viewport.xscale;
		clip.scale[1]        = viewport.yscale;
		clip.offset[0]       = viewport.xoffset;
		clip.offset[1]       = viewport.yoffset;
		clip.half_extent[0] =
		    static_cast<float>(std::min(limits.maxViewportDimensions[0], 16384u)) * 0.5f;
		clip.half_extent[1] =
		    static_cast<float>(std::min(limits.maxViewportDimensions[1], 16384u)) * 0.5f;
		clip.enabled = true;
		clip.dynamic = true;
	}
	uint32_t push_data_cursor =
	    mesh_active ? ShaderRecompiler::IR::PushData::MeshDrawDwordCount : 0;
	if (vertex_info[tess_active ? 2u : 0u].clip_space.enabled) {
		push_data_cursor += ShaderRecompiler::IR::PushData::ClipTransformDwordCount;
	}
	GraphicsPrograms result;
	if (pixel_active) {
		result.pixel = m_program_cache->Get(pixel_params, pixel_info, push_data_cursor);
	}
	for (uint32_t i = 0; i < (tess_active ? 3u : 1u); i++) {
		result.vertex[i] = m_program_cache->Get(vertex_params[i], vertex_info[i], push_data_cursor);
	}
	return result;
}

ShaderProgram PipelineCache::GetComputeProgram(const HW::ComputeShaderInfo& regs,
                                               const HW::ShaderRegisters&   sh,
                                               ShaderComputeInputInfo&      input_info) {
	input_info.host_subgroup_size = m_graphics.SupportsComputeWave64() ? 64u : 32u;
	// Load-window attribution (Dev 118): isolate the un-zoned PrepareProgram step from the
	// program-cache lookup inside CS::GetProgram. Inert without Tracy.
	const auto params = [&] {
		KYTY_PROFILER_BLOCK("CS::PrepareProgram");
		return PrepareProgram(regs, sh, input_info);
	}();
	// Use one effective size for the cache key, LDS declaration, and access bounds.
	const auto max_lds_dwords =
	    m_graphics.GetPhysicalDeviceProperties().limits.maxComputeSharedMemorySize / 4u;
	if (input_info.lds_size_dwords > max_lds_dwords) {
		static std::atomic_bool warned = false;
		if (!warned.exchange(true, std::memory_order_relaxed)) {
			PipelineCacheLog("GPU warning: game compute shader requests {} bytes of LDS, but "
			                 "the Vulkan device limit is {} bytes. Clamping LDS; rendering may "
			                 "be incorrect.",
			                 input_info.lds_size_dwords * 4u, max_lds_dwords * 4u);
		}
	}
	input_info.lds_size_dwords = std::min(input_info.lds_size_dwords, max_lds_dwords);
	uint32_t push_data_cursor  = 0;
	KYTY_PROFILER_BLOCK("CS::ProgramCacheGet");
	return m_program_cache->Get(params, input_info, push_data_cursor);
}

bool PipelineStaticParameters::operator==(const PipelineStaticParameters& other) const noexcept {
	return std::memcmp(this, &other, sizeof(*this)) == 0;
}

PipelineCache::Pipeline& PipelineCache::GetGraphicsPipeline(
    std::span<const RenderColorInfo> colors, const RenderDepthInfo& depth,
    std::span<const ShaderVertexInputInfo> vertex_info, CommandBuffer& command,
    const ShaderPixelInputInfo* ps_input_info, vk::PrimitiveTopology topology,
    bool primitive_restart_enable, const GraphicsPrograms& programs) {
	const auto& vs_input_info  = vertex_info.front();
	const auto& vertex_program = programs.vertex[0];
	const auto& pixel_program  = programs.pixel;
	KYTY_PROFILER_BLOCK("PipelineCache::CreatePipeline(Gfx)", profiler::colors::DeepOrangeA200);

	EXIT_IF(colors.size() > RENDER_COLOR_ATTACHMENTS_MAX);
	EXIT_IF(!vertex_program);
	const bool ps_active = ps_input_info != nullptr;
	EXIT_IF(ps_active && !pixel_program);
	const auto color_count = static_cast<uint32_t>(colors.size());

	auto& ctx = command.GetRegisters();

	const HW::ModeControl& mc = ctx.GetModeControl();

	const auto vs_id = vertex_program.id;
	const auto ps_id = ps_active ? pixel_program.id : 0;

	GraphicsPipelineKey key {};
	for (uint32_t i = 0; i < programs.vertex.size(); i++) {
		key.vertex_shader_ids[i] = programs.vertex[i].id;
	}
	key.ps_shader_id            = ps_id;
	auto& static_params         = key.static_params;
	auto& rendering             = key.rendering;
	rendering.color_count       = 0;
	uint32_t attachment_samples = 0;
	for (uint32_t i = 0; i < color_count; i++) {
		const auto slot = colors[i].target_slot;
		EXIT_IF(slot >= RENDER_COLOR_ATTACHMENTS_MAX);
		rendering.color_count = std::max(rendering.color_count, slot + 1);
		EXIT_IF(!colors[i].image_id || colors[i].desc.view_info.format == vk::Format::eUndefined);
		static_params.color_mask[slot] = colors[i].export_mapping.ApplyMask(
		    render_target_mask_slot(ctx.GetRenderTargetMask(), colors[i].target_slot));
		rendering.color_formats[slot] = colors[i].desc.view_info.format;
		if (attachment_samples == 0) {
			attachment_samples = colors[i].desc.info.samples;
		} else if (attachment_samples != colors[i].desc.info.samples) {
			EXIT("mixed color attachment sample counts are unsupported: %u and %u\n",
			     attachment_samples, colors[i].desc.info.samples);
		}
		const auto& rt = ctx.GetRenderTarget(colors[i].target_slot);
		const auto& bc = ctx.GetBlendControl(colors[i].target_slot);
		const bool  alpha_remap =
		    slot == 0 && ps_input_info != nullptr && ps_input_info->alpha_blend_source_remap;
		static_params.blend_enable[slot] = bc.enable && !rt.info.blend_bypass;
		if (static_params.blend_enable[slot] && !alpha_remap &&
		    ClassifyBlendMapping(bc, colors[i].export_mapping) != BlendMappingSupport::Direct) {
			static_params.blend_enable[slot] = false;
			static std::atomic_bool warned   = false;
			if (!warned.exchange(true, std::memory_order_relaxed)) {
				Log::WriteToConsoleAndLog(fmt::format(
				    "Warning: blending disabled for unsupported color mapping "
				    "(slot={} mapping=0x{:02x} color={}/{} alpha={}/{} separate={}).\n",
				    slot, colors[i].export_mapping.packed, bc.color_srcblend, bc.color_destblend,
				    bc.alpha_srcblend, bc.alpha_destblend, bc.separate_alpha_blend ? 1 : 0));
			}
		}
		if (alpha_remap) {
			static_params.blend_alpha_source_remap = true;
		}
		if (static_params.blend_enable[slot]) {
			static_params.color_srcblend[slot]       = bc.color_srcblend;
			static_params.color_comb_fcn[slot]       = bc.color_comb_fcn;
			static_params.color_destblend[slot]      = bc.color_destblend;
			static_params.separate_alpha_blend[slot] = bc.separate_alpha_blend;
			if (bc.separate_alpha_blend) {
				static_params.alpha_srcblend[slot]  = bc.alpha_srcblend;
				static_params.alpha_comb_fcn[slot]  = bc.alpha_comb_fcn;
				static_params.alpha_destblend[slot] = bc.alpha_destblend;
			}
		}
	}
	const bool with_depth =
	    depth.desc.view_info.format != vk::Format::eUndefined && static_cast<bool>(depth.image_id);
	if (with_depth) {
		const auto aspects       = ImageViewOps::DepthAspectMask(depth.desc.view_info.format);
		rendering.depth_format   = aspects & vk::ImageAspectFlagBits::eDepth
		                               ? depth.desc.view_info.format
		                               : vk::Format::eUndefined;
		rendering.stencil_format = aspects & vk::ImageAspectFlagBits::eStencil
		                               ? depth.desc.view_info.format
		                               : vk::Format::eUndefined;
		if (attachment_samples == 0) {
			attachment_samples = depth.desc.info.samples;
		} else if (attachment_samples != depth.desc.info.samples) {
			EXIT("mixed color/depth sample counts are unsupported: %u and %u\n", attachment_samples,
			     depth.desc.info.samples);
		}
	}
	if (color_count == 0 && !with_depth) {
		attachment_samples = render_sample_count(ctx.GetAaConfig().msaa_num_samples);
		EXIT_IF(!static_cast<bool>(
		    m_graphics.GetPhysicalDeviceProperties().limits.framebufferNoAttachmentsSampleCounts &
		    vulkan_sample_count(attachment_samples)));
	}
	EXIT_IF(attachment_samples == 0 ||
	        vulkan_sample_count(attachment_samples) == vk::SampleCountFlagBits {});

	if (ps_active && depth.depth_test_enable && ps_input_info->ps_execute_on_noop) {
		static std::atomic<uint32_t> log_count {0};
		if (log_count.fetch_add(1, std::memory_order_relaxed) < 16) {
			LOGF("Pipeline: temporary: accepting EXEC_ON_NOOP with depth test enabled\n");
		}
	}

	const auto& clip_control               = ctx.GetClipControl();
	static_params.negative_one_to_one      = !clip_control.dx_clip_space;
	static_params.depth_clip_enable        = clip_control.IsZClipEnabled();
	static_params.topology                 = topology;
	static_params.primitive_restart_enable = primitive_restart_enable;
	static_params.samples                  = attachment_samples;
	static_params.sample_shading_enable =
	    ps_active && attachment_samples > 1 && ps_input_info->ps_sample_shading;
	if (static_params.sample_shading_enable && !m_graphics.sample_rate_shading_enabled) {
		EXIT("Pipeline: sample-rate shading is required but unsupported by the host\n");
	}
	static_params.depth_bounds_test_enable = depth.depth_bounds_test_enable;
	static_params.depth_min_bounds         = depth.depth_min_bounds;
	static_params.depth_max_bounds         = depth.depth_max_bounds;
	const bool rect_list             = Prospero::IsRectList(command.GetUserConfig().GetPrimType());
	static_params.cull_back          = !rect_list && mc.cull_back;
	static_params.cull_front         = !rect_list && mc.cull_front;
	static_params.face               = mc.face;
	static_params.provoking_vtx_last = mc.provoking_vtx_last;
	static_params.polygon_mode =
	    ResolvePolygonMode(mc, static_params.cull_front, static_params.cull_back);

	if (vs_input_info.stage.program->stage != ShaderType::Mesh) {
		EXIT_IF(vs_input_info.buffers_num < 0 ||
		        vs_input_info.buffers_num > ShaderVertexInputInfo::RES_MAX ||
		        vs_input_info.resources_num < 0 ||
		        vs_input_info.resources_num > ShaderVertexInputInfo::RES_MAX);
		key.vertex_input.binding_count   = static_cast<uint8_t>(vs_input_info.buffers_num);
		key.vertex_input.attribute_count = static_cast<uint8_t>(vs_input_info.resources_num);
		for (int binding = 0; binding < vs_input_info.buffers_num; binding++) {
			const auto& buffer                 = vs_input_info.buffers[binding];
			key.vertex_input.bindings[binding] = {.stride   = buffer.stride,
			                                      .instance = buffer.fetch_index != 0};
		}
		for (int attribute = 0; attribute < vs_input_info.resources_num; attribute++) {
			const auto binding = vs_input_info.resources_dst[attribute].buffer_index;
			EXIT_IF(binding < 0 || binding >= vs_input_info.buffers_num);
			key.vertex_input.attributes[attribute] = {
			    .offset  = static_cast<uint32_t>(vs_input_info.resources[attribute].Base48() -
			                                     vs_input_info.buffers[binding].addr),
			    .binding = static_cast<uint8_t>(binding),
			};
		}
	}

	if (auto iter = m_graphics_pipelines.find(key); iter != m_graphics_pipelines.end()) {
		return *iter->second;
	}

	if (graphics_debug_dump_enabled()) {
		ShaderDbgDumpInputInfo(vs_input_info);
		if (ps_active) {
			ShaderDbgDumpInputInfo(*ps_input_info);
		}
		LOGF("PipelineTrace: shader modules VS=%" PRIu64 " module=%p PS=%" PRIu64 " module=%p\n",
		     vs_id, static_cast<void*>(vertex_program.module), ps_id,
		     static_cast<void*>(pixel_program.module));
	}

	auto cached = std::make_unique<Pipeline>();
	LogPipelineTrace("CreatePipelineInternal begin", vs_id, ps_id);
	CreatePipelineInternal(m_graphics, *cached, rendering, key.vertex_input, vertex_info,
	                       ps_input_info, programs, static_params, m_driver_cache);
	LogPipelineTrace("CreatePipelineInternal done", vs_id, ps_id);

	EXIT_NOT_IMPLEMENTED(cached->pipeline == nullptr);
	EXIT_NOT_IMPLEMENTED(cached->pipeline_layout == nullptr);

	auto [iter, inserted] = m_graphics_pipelines.emplace(std::move(key), std::move(cached));
	EXIT_IF(!inserted);

	return *iter->second;
}

PipelineCache::Pipeline& PipelineCache::GetComputePipeline(const ShaderComputeInputInfo& input_info,
                                                           const ShaderProgram& compute_program) {
	KYTY_PROFILER_BLOCK("PipelineCache::CreatePipeline(Compute)", profiler::colors::RedA100);

	EXIT_IF(!compute_program);

	if (auto iter = m_compute_pipelines.find(compute_program.id);
	    iter != m_compute_pipelines.end()) {
		return *iter->second;
	}

	if (graphics_debug_dump_enabled()) {
		ShaderDbgDumpInputInfo(input_info);
	}

	auto cached = std::make_unique<Pipeline>();
	CreatePipelineInternal(m_graphics, *cached, input_info, compute_program.module, m_driver_cache);

	EXIT_NOT_IMPLEMENTED(cached->pipeline == nullptr);
	EXIT_NOT_IMPLEMENTED(cached->pipeline_layout == nullptr);

	auto [iter, inserted] = m_compute_pipelines.emplace(compute_program.id, std::move(cached));
	EXIT_IF(!inserted);

	return *iter->second;
}
} // namespace Libs::Graphics
