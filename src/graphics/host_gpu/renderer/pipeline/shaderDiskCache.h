#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_SHADERDISKCACHE_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_PIPELINE_SHADERDISKCACHE_H_
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <type_traits>

namespace Libs::Graphics::ShaderDiskCache {
using Bytes = std::vector<uint8_t>;
namespace IR = ShaderRecompiler::IR;

// Senaxx 9685da27: exact source/permutation keys and field-wise IR encoding. Files
// contain no Vulkan handles, host instruction pointers, snapshots or native JIT code.
struct ByteWriter {
	Bytes bytes;
	template<typename T> void Put(T value) {
		if constexpr (std::is_same_v<T, bool>) bytes.push_back(value ? 1 : 0);
		else if constexpr (std::is_enum_v<T>) Put(static_cast<std::underlying_type_t<T>>(value));
		else {
			static_assert(std::is_integral_v<T>);
			using U = std::make_unsigned_t<T>;
			const U bits = static_cast<U>(value);
			for (size_t i=0;i<sizeof(T);i++) bytes.push_back(static_cast<uint8_t>(bits >> (8*i)));
		}
	}
	void PutBytes(std::span<const uint8_t> value) { bytes.insert(bytes.end(),value.begin(),value.end()); }
	void String(std::string_view value) { Put<uint64_t>(value.size()); PutBytes({reinterpret_cast<const uint8_t*>(value.data()),value.size()}); }
	void Words(std::span<const uint32_t> value) { Put<uint64_t>(value.size()); for(auto word:value) Put(word); }
};

bool EncodeSource(const IR::ResourcePlan& plan, Bytes& payload);
bool DecodeSource(std::span<const uint8_t> payload, IR::ResourcePlan& plan);
bool EncodePermutation(std::span<const uint32_t> spirv,const IR::CompiledShaderInfo& program,Bytes& payload);
bool DecodePermutation(std::span<const uint8_t> payload,std::vector<uint32_t>& spirv,IR::CompiledShaderInfo& program);
void AppendSpecialization(ByteWriter& key,const IR::ResourceSpecialization& specialization);
std::string_view BuildFingerprint();
void AppendSession(ByteWriter& key);

class Store {
public:
	explicit Store(std::filesystem::path directory);
	~Store();
	Store(const Store&) = delete;
	Store& operator=(const Store&) = delete;
	bool Load(std::span<const uint8_t> key,Bytes& payload);
	void Save(std::span<const uint8_t> key,Bytes payload);
	void Flush();
	struct Counters { uint64_t hits=0, misses=0, rejected=0, queued=0, dropped=0; } counters;
private:
	struct Pending { std::filesystem::path file; Bytes contents; };
	void Writer(std::stop_token stop);
	std::filesystem::path Path(std::span<const uint8_t> key) const;
	std::filesystem::path m_directory;
	std::mutex m_mutex;
	std::condition_variable_any m_available;
	std::condition_variable m_idle;
	std::deque<Pending> m_pending;
	size_t m_pending_bytes = 0;
	bool m_active = false;
	std::jthread m_writer;
};
} // namespace Libs::Graphics::ShaderDiskCache
#endif
