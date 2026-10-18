#include "graphics/host_gpu/renderer/pipeline/shaderDiskCache.h"
#include "shaderCacheVersion.h"
#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <xxhash.h>

namespace Libs::Graphics::ShaderDiskCache {
namespace {
constexpr uint32_t Magic=0x43444655, Format=1;
constexpr uint64_t MaxFile=256ull<<20, AllocationBudget=128ull<<20;
template<class T> struct IsVector:std::false_type{};
template<class T> struct IsVector<std::vector<T>>:std::true_type{};
template<class T> struct IsArray:std::false_type{};
template<class T,size_t N> struct IsArray<std::array<T,N>>:std::true_type{};
template<class T> struct IsOptional:std::false_type{};
template<class T> struct IsOptional<std::optional<T>>:std::true_type{};

// Adapted from Senaxx 9685da27. List EVERY persisted field, excluding process-local scratch.
#define CACHE_FIELDS(Type, ...) \
template<class Io,class V> requires std::same_as<std::remove_const_t<V>,Type> \
void Visit(Io& io,V& v) { io.Fields(__VA_ARGS__); }
CACHE_FIELDS(IR::MemoryInfo,v.kind,v.resource,v.sampler,v.offset,v.secondary_offset,v.dmask,
 v.data_dwords,v.data_bits,v.component_index,v.component_count,v.data_format,v.number_format,
 v.image_sample_flags,v.image_dimension,v.image_address_components,v.address_is_full,v.data_signed,
 v.typed,v.formatted,v.image_has_mip,v.image_r128,v.idxen,v.offen,v.coherent,v.planning_only)
CACHE_FIELDS(IR::BufferResource,v.source,v.first_use_pc,v.max_byte_extent,v.packed_stride,
 v.descriptor_format,v.descriptor_swizzle,v.image_alias,v.read,v.written,v.atomic,v.formatted,v.scalar)
CACHE_FIELDS(IR::ImageResource,v.source,v.first_use_pc,v.resource_class,v.numeric_class,v.dimension,
 v.mip_mode,v.mip_count,v.conversion_format,v.shader_swizzle,v.read,v.written,v.atomic,v.atomic64,
 v.depth_compare,v.cube,v.r128,v.indirect_root,v.indirect_mapping_offset,v.indirect_search_iterations,v.indirect_resources)
CACHE_FIELDS(IR::SamplerResource,v.source,v.first_use_pc,v.snapshot_index,v.force_point_filtering,
 v.depth_compare,v.integer_border,v.gather_lod)
CACHE_FIELDS(IR::SampledResourcePair,v.image,v.sampler,v.first_use_pc)
CACHE_FIELDS(IR::StageInput,v.kind,v.location,v.component_count,v.debug_name,v.per_vertex)
CACHE_FIELDS(IR::StageOutput,v.kind,v.index,v.location,v.debug_name)
CACHE_FIELDS(IR::ShaderInfo,v.buffers,v.images,v.samplers,v.sampled_pairs,v.inputs,v.outputs,
 v.vertex_fetch_components,v.vertex_offset_sgpr,v.instance_offset_sgpr,v.has_bitwise_xor,v.uses_dma)
CACHE_FIELDS(IR::DescriptorBinding,v.kind,v.resources)
CACHE_FIELDS(IR::BindingLayout,v.push_data_start_dword,v.memory_offset_dword,v.memory_offset_count,
 v.user_data_registers,v.descriptors)
CACHE_FIELDS(IR::CompiledShaderInfo,v.stage,v.shader_hash,v.wave_size,v.user_data_base,v.user_data_count,
 v.scratch_dwords,v.param_export_mask,v.has_address_writes,v.info,v.bindings)
CACHE_FIELDS(IR::DescriptorSource::IndirectImage,v.material_source,v.table_source,v.selector_stride,
 v.selector_offset,v.table_offset,v.key_count,v.selector_mask,v.sources)
CACHE_FIELDS(IR::DescriptorSource,v.dwords,v.dword_count,v.indirect_image)
CACHE_FIELDS(IR::ResourceBlock,v.condition,v.successors,v.sources,v.srt_reads)
CACHE_FIELDS(IR::SrtRead,v.value,v.flat_offset)
CACHE_FIELDS(IR::UniformFill,v.kind,v.resource,v.group_stride,v.words,v.value)
CACHE_FIELDS(IR::UniformFillPlan,v.fill,v.values)
CACHE_FIELDS(IR::ResourceSpecialization::Buffer,v.packed_stride,v.descriptor_format,v.descriptor_swizzle,v.zero_stride_oob)
CACHE_FIELDS(IR::ResourceSpecialization::Image,v.numeric_class,v.dimension,v.mip_count,v.conversion_format,
 v.shader_swizzle,v.indirect_root,v.indirect_mapping_offset,v.indirect_search_iterations,v.cube,v.fmask)
CACHE_FIELDS(IR::ResourceSpecialization,v.buffers,v.images)
#undef CACHE_FIELDS
template<class Io,class Plan> void PlanFields(Io& io,Plan& p) {
	io.Fields(p.stage,p.shader_hash,p.user_data_base,p.user_data_count,p.memory_info,p.descriptor_sources,
	 p.control_flow,p.srt_reads,p.clean_flat_slots,p.requires_specialization_memory,p.capture_specialization_reads,
	 p.srt_plan_complete,p.resource_tracking_complete,p.info,p.uniform_fill);
}

class ByteReader {
public:
	explicit ByteReader(std::span<const uint8_t> data): bytes(data) {}
	template<class T> bool Get(T& value) {
		if(!ok || sizeof(T)>Remaining()) return Fail();
		if constexpr(std::is_same_v<T,bool>) {
			const auto b=bytes[cursor++]; if(b>1) return Fail(); value=b!=0;
		} else if constexpr(std::is_enum_v<T>) {
			std::underlying_type_t<T> raw{}; if(!Get(raw)) return false; value=static_cast<T>(raw);
		} else {
			static_assert(std::is_integral_v<T>); using U=std::make_unsigned_t<T>; U raw=0;
			for(size_t i=0;i<sizeof(T);i++) raw|=static_cast<U>(bytes[cursor++])<<(8*i);
			value=static_cast<T>(raw);
		}
		return true;
	}
	bool Count(uint64_t& count,size_t allocation) {
		if(!Get(count)||count>Remaining()||count>(1u<<20)||count>budget/allocation) return Fail();
		budget-=count*allocation; return true;
	}
	bool Fail() { ok=false; return false; }
	size_t Remaining() const { return bytes.size()-cursor; }
	bool Done() const { return ok && cursor==bytes.size(); }
	std::span<const uint8_t> bytes;
	size_t cursor=0;
	uint64_t budget=AllocationBudget;
	bool ok=true;
};

struct Encoder {
	ByteWriter& out;
	const std::unordered_map<const IR::Inst*,uint32_t>* indices=nullptr;
	bool ok=true;
	template<class... T> void Fields(const T&... values) { ((*this)(values),...); }
	template<class T> void operator()(const T& value) {
		if constexpr(std::is_arithmetic_v<T>||std::is_enum_v<T>) {
			if constexpr(std::is_floating_point_v<T>) out.Put(std::bit_cast<uint32_t>(value)); else out.Put(value);
		} else if constexpr(IsArray<T>::value||IsVector<T>::value) {
			if constexpr(IsVector<T>::value) out.Put<uint64_t>(value.size());
			for(const auto& item:value) (*this)(item);
		} else if constexpr(std::is_same_v<T,std::string>) out.String(value);
		else if constexpr(IsOptional<T>::value) { out.Put(value.has_value()); if(value) (*this)(*value); }
		else if constexpr(std::is_same_v<T,IR::Value>) {
			if(auto* inst=value.TryInstruction()) {
				if(!indices) { ok=false; return; }
				const auto found=indices->find(inst); if(found==indices->end()) { ok=false; return; }
				out.Put<uint8_t>(1); out.Put(found->second);
			} else { out.Put<uint8_t>(0); out.Put(value.GetType()); out.Put(value.ImmediateBits()); }
		} else Visit(*this,value);
	}
};
struct Decoder {
	ByteReader& in;
	const std::vector<IR::Inst*>* instructions=nullptr;
	template<class... T> void Fields(T&... values) { ((*this)(values),...); }
	template<class T> void operator()(T& value) {
		if(!in.ok) return;
		if constexpr(std::is_arithmetic_v<T>||std::is_enum_v<T>) {
			if constexpr(std::is_floating_point_v<T>) { uint32_t bits=0; if(in.Get(bits)) value=std::bit_cast<float>(bits); }
			else in.Get(value);
		} else if constexpr(IsVector<T>::value) {
			uint64_t count=0; if(!in.Count(count,sizeof(typename T::value_type))) return;
			value.clear(); value.resize(static_cast<size_t>(count)); for(auto& item:value) (*this)(item);
		} else if constexpr(IsArray<T>::value) { for(auto& item:value) (*this)(item); }
		else if constexpr(std::is_same_v<T,std::string>) {
			uint64_t count=0; if(!in.Count(count,1)||count>65536) { in.Fail(); return; }
			value.assign(reinterpret_cast<const char*>(in.bytes.data()+in.cursor),static_cast<size_t>(count)); in.cursor+=count;
		} else if constexpr(IsOptional<T>::value) {
			bool present=false; if(!in.Get(present)) return; value.reset();
			if(present) { value=typename T::value_type{}; (*this)(*value); }
		} else if constexpr(std::is_same_v<T,IR::Value>) {
			uint8_t tag=0; if(!in.Get(tag)) return;
			if(tag==1) { uint32_t index=0; if(!in.Get(index)||!instructions||index>=instructions->size()) in.Fail(); else value=IR::Value((*instructions)[index]); }
			else if(tag==0) { IR::Type type{}; uint64_t bits=0; if(!in.Get(type)||!in.Get(bits)||!IR::Value::FromImmediateBits(type,bits,value)) in.Fail(); }
			else in.Fail();
		} else Visit(*this,value);
	}
};

bool ValidInfo(const IR::ShaderInfo& info) {
	return info.buffers.size()<=IR::ShaderInfo::MaxBuffers && info.images.size()<=IR::ShaderInfo::MaxImages &&
	 info.samplers.size()<=IR::ShaderInfo::MaxSamplers && info.sampled_pairs.size()<=IR::ShaderInfo::MaxSampledPairs;
}
bool ValidPlan(const IR::ResourcePlan& p) {
	if(p.user_data_count>128||!ValidInfo(p.info)||p.clean_flat_slots.size()>p.srt_reads.size()) return false;
	for(const auto& source:p.descriptor_sources) {
		if(source.dword_count>8) return false;
		if(source.indirect_image) for(auto id:source.indirect_image->sources) if(id>=p.descriptor_sources.size()) return false;
	}
	for(const auto& b:p.control_flow) {
		for(auto id:b.successors) if(id>=p.control_flow.size()) return false;
		for(auto id:b.sources) if(id>=p.descriptor_sources.size()) return false;
		for(auto id:b.srt_reads) if(id>=p.srt_reads.size()) return false;
	}
	for(const auto& b:p.info.buffers) if(b.source>=p.descriptor_sources.size()) return false;
	for(const auto& i:p.info.images) if(i.source>=p.descriptor_sources.size()) return false;
	for(const auto& s:p.info.samplers) if(s.source>=p.descriptor_sources.size()) return false;
	return true;
}

bool WriteAtomic(const std::filesystem::path& path,std::span<const uint8_t> bytes) {
	auto temp=path; temp+=".tmp";
	{
		std::ofstream file(temp,std::ios::binary|std::ios::trunc);
		if(!file || !file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) return false;
		file.close(); if(!file) return false;
	}
	std::error_code error;
	std::filesystem::rename(temp,path,error);
	if(error) {
		// Windows rename cannot replace an existing cache entry. A transient miss is safe;
		// readers only ever see a complete old or new file, never the temporary payload.
		error.clear(); std::filesystem::remove(path,error); error.clear();
		std::filesystem::rename(temp,path,error);
	}
	if(error) { std::filesystem::remove(temp,error); return false; }
	return true;
}
} // namespace

bool EncodeSource(const IR::ResourcePlan& p,Bytes& payload) {
	ByteWriter out;
	std::unordered_map<const IR::Inst*,uint32_t> indices;
	for(const auto& inst:p.value_storage) indices.emplace(&inst,static_cast<uint32_t>(indices.size()));
	Encoder io{out,&indices}; out.Put<uint64_t>(p.value_storage.size());
	for(const auto& inst:p.value_storage) { out.Put(inst.GetOpcode()); out.Put(inst.Flags<uint64_t>()); }
	for(const auto& inst:p.value_storage) {
		out.Put<uint32_t>(static_cast<uint32_t>(inst.NumArgs()));
		for(size_t i=0;i<inst.NumArgs();i++) {
			if(inst.GetOpcode()==IR::ValueOpcode::Phi && inst.PhiBlock(i)!=nullptr) io.ok=false;
			io(inst.Arg(i));
		}
	}
	PlanFields(io,p);
	if(!io.ok||out.bytes.size()>MaxFile||!ValidPlan(p)) return false;
	payload=std::move(out.bytes); return true;
}
bool DecodeSource(std::span<const uint8_t> payload,IR::ResourcePlan& plan) {
	ByteReader in(payload); uint64_t count=0;
	if(!in.Count(count,sizeof(IR::Inst))) return false;
	IR::ResourcePlan result;
	std::vector<IR::Inst*> instructions; instructions.reserve(static_cast<size_t>(count));
	for(uint64_t i=0;i<count;i++) {
		IR::ValueOpcode opcode{}; uint64_t flags=0;
		if(!in.Get(opcode)||!in.Get(flags)||opcode>=IR::ValueOpcode::Count) return false;
		instructions.push_back(&result.value_storage.emplace_back(opcode,flags));
	}
	Decoder io{in,&instructions};
	for(auto* inst:instructions) {
		uint32_t args=0; if(!in.Get(args)) return false;
		const bool phi=inst->GetOpcode()==IR::ValueOpcode::Phi;
		if((!phi && args!=inst->NumArgs())||args>65535) return false;
		for(uint32_t i=0;i<args;i++) {
			IR::Value value; io(value); if(!in.ok) return false;
			if(phi) inst->AddPhiOperand(nullptr,value); else inst->SetArg(i,value);
		}
	}
	PlanFields(io,result);
	if(!in.Done()||!ValidPlan(result)) return false;
	plan=std::move(result); return true;
}
bool EncodePermutation(std::span<const uint32_t> spirv,const IR::CompiledShaderInfo& program,Bytes& payload) {
	if(spirv.size()<5||spirv.front()!=0x07230203u||!ValidInfo(program.info)) return false;
	ByteWriter out; Encoder io{out}; out.Words(spirv); io(program);
	if(!io.ok||out.bytes.size()>MaxFile) return false;
	payload=std::move(out.bytes); return true;
}
bool DecodePermutation(std::span<const uint8_t> payload,std::vector<uint32_t>& spirv,IR::CompiledShaderInfo& program) {
	ByteReader in(payload); Decoder io{in};
	std::vector<uint32_t> code; IR::CompiledShaderInfo result;
	io.Fields(code,result);
	if(!in.Done()||code.size()<5||code.front()!=0x07230203u||!ValidInfo(result.info)||result.user_data_count>128 ||
	   (result.bindings.UsesPushData() && !IR::PushData::CanFit(result.bindings.push_data_start_dword,result.bindings.ShaderDataDwords()))) return false;
	for(const auto& binding:result.bindings.descriptors) {
		if(binding.kind>=IR::DescriptorBindingKind::Count) return false;
	}
	spirv=std::move(code); program=std::move(result); return true;
}
void AppendSpecialization(ByteWriter& key,const IR::ResourceSpecialization& specialization) { Encoder io{key}; io(specialization); }
std::string_view BuildFingerprint() { return ShaderCacheVersion::Fingerprint; }
void AppendSession(ByteWriter& key) {
	key.Put(Magic); key.Put(Format); key.String(BuildFingerprint());
	for(const auto name:ShaderCacheVersion::Switches) {
		const std::string owned(name); const char* value=std::getenv(owned.c_str());
		key.String(name); key.Put(value!=nullptr); if(value) key.String(value);
	}
}

Store::Store(std::filesystem::path directory):m_directory(std::move(directory)) {
	std::error_code error; std::filesystem::create_directories(m_directory,error);
	m_writer=std::jthread([this](std::stop_token stop){ Writer(stop); });
}
Store::~Store() { Flush(); m_writer.request_stop(); m_available.notify_all(); if(m_writer.joinable()) m_writer.join(); }
std::filesystem::path Store::Path(std::span<const uint8_t> key) const {
	const auto hash=XXH3_128bits(key.data(),key.size());
	char name[80]; std::snprintf(name,sizeof(name),"ufc-%016llx%016llx.bin",
	 static_cast<unsigned long long>(hash.high64),static_cast<unsigned long long>(hash.low64));
	return m_directory/name;
}
bool Store::Load(std::span<const uint8_t> key,Bytes& payload) {
	std::ifstream file(Path(key),std::ios::binary|std::ios::ate);
	if(!file) { counters.misses++; return false; }
	const auto length=file.tellg();
	if(length<0||static_cast<uint64_t>(length)>MaxFile) { counters.rejected++; return false; }
	Bytes contents(static_cast<size_t>(length)); file.seekg(0);
	if(!file.read(reinterpret_cast<char*>(contents.data()),static_cast<std::streamsize>(contents.size()))) { counters.rejected++; return false; }
	ByteReader in(contents); uint32_t magic=0,format=0; uint64_t key_size=0,payload_size=0,hash=0;
	if(!in.Get(magic)||!in.Get(format)||!in.Get(key_size)||!in.Get(payload_size)||!in.Get(hash)||
	   magic!=Magic||format!=Format||key_size!=key.size()||key_size>in.Remaining()||payload_size!=in.Remaining()-key_size ||
	   !std::equal(key.begin(),key.end(),contents.begin()+in.cursor) ||
	   XXH3_64bits(contents.data()+in.cursor+key_size,static_cast<size_t>(payload_size))!=hash) { counters.rejected++; return false; }
	payload.assign(contents.begin()+in.cursor+key_size,contents.end()); counters.hits++; return true;
}
void Store::Save(std::span<const uint8_t> key,Bytes payload) {
	if(key.size()>MaxFile-32||payload.size()>MaxFile-32-key.size()) { counters.dropped++; return; }
	ByteWriter out; out.Put(Magic); out.Put(Format); out.Put<uint64_t>(key.size()); out.Put<uint64_t>(payload.size());
	out.Put(XXH3_64bits(payload.data(),payload.size())); out.PutBytes(key); out.PutBytes(payload);
	std::lock_guard lock(m_mutex);
	if(out.bytes.size()>(64ull<<20)||m_pending_bytes>(64ull<<20)-out.bytes.size()) { counters.dropped++; return; }
	m_pending_bytes+=out.bytes.size(); m_pending.push_back({Path(key),std::move(out.bytes)}); counters.queued++; m_available.notify_one();
}
void Store::Flush() { std::unique_lock lock(m_mutex); m_idle.wait(lock,[this]{ return m_pending.empty()&&!m_active; }); }
void Store::Writer(std::stop_token stop) {
	for(;;) {
		Pending job;
		{
			std::unique_lock lock(m_mutex);
			if(!m_available.wait(lock,stop,[this]{ return !m_pending.empty(); })) return;
			job=std::move(m_pending.front()); m_pending.pop_front(); m_active=true;
		}
		(void)WriteAtomic(job.file,job.contents);
		{
			std::lock_guard lock(m_mutex); m_pending_bytes-=job.contents.size(); m_active=false;
		}
		m_idle.notify_all();
	}
}
} // namespace Libs::Graphics::ShaderDiskCache
