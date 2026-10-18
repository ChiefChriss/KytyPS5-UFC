#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_

#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

namespace Libs::Graphics::ShaderRecompiler::IR {

// Canonical module-affecting resource state. Runtime addresses and descriptor payloads remain in
// ResourceSnapshot and therefore do not create shader permutations.
struct ResourceSpecialization {
	struct Buffer {
		uint32_t               packed_stride                   = 0;
		Prospero::BufferFormat descriptor_format               = Prospero::BufferFormat::kInvalid;
		uint32_t               descriptor_swizzle              = DstSel(4, 5, 6, 7);
		bool                   zero_stride_oob                 = false;
		bool                   operator==(const Buffer&) const = default;
	};

	struct Image {
		Prospero::TextureNumericClass numeric_class = Prospero::TextureNumericClass::Unsupported;
		Decoder::ImageDimension       dimension     = Decoder::ImageDimension::Unknown;
		uint32_t                      mip_count     = 1;
		Prospero::BufferFormat        conversion_format          = Prospero::BufferFormat::kInvalid;
		uint32_t                      shader_swizzle             = ShaderImageIdentitySwizzle;
		uint32_t                      indirect_root              = ImageResource::NoIndirectImage;
		uint32_t                      indirect_mapping_offset    = 0;
		uint32_t                      indirect_search_iterations = 0;
		bool                          cube                       = false;
		bool                          fmask                      = false;
		bool                          operator==(const Image&) const = default;
	};

	std::vector<Buffer> buffers;
	std::vector<Image>  images;

	bool operator==(const ResourceSpecialization&) const = default;
};

// Extracts the descriptor/SRT value graph before resource specialization. The returned plan owns
// its values and is independent of the translated shader CFG.
ResourcePlan ExtractResourcePlan(const Program& program);

// Every guest-memory read a materialization performed, in order (`values` holds the words of
// successful reads). Evaluation is a pure function of the program, the runtime user data and
// shader base, and these read results, so an unchanged replay proves an unchanged result.
struct SrtReadRecord {
	uint64_t address = 0;
	uint32_t first   = 0;
	uint32_t count   = 0;
	bool     strict  = false;
	bool     ok      = false;
};
struct SrtReadLog {
	std::vector<SrtReadRecord> records;
	std::vector<uint32_t>      values;
	// User-data words the evaluation read (SrtRuntime::user_data_reads); unread words cannot
	// affect the result, except through ResourceSnapshot::user_data, which copies them all.
	uint64_t                   user_data_mask = 0;
};

// Refreshes cached resources and specialization in place. A failed refresh must not be used.
// With `log`, also records every guest-memory read (see SrtReadLog).
bool MaterializeResources(const ResourcePlan& program, const SrtRuntime& runtime,
                          ResourceSnapshot& snapshot, ResourceSpecialization& specialization,
                          SrtReadLog* log = nullptr);

// Re-performs the logged reads in order with `runtime`'s readers and stops at the first read
// whose success or words differ. Returns true when every read is unchanged. `replayed` counts
// the reads performed.
bool ReplaySrtReads(const SrtRuntime& runtime, const SrtReadLog& log, uint64_t& replayed);

// Applies an already-derived specialization to native IR before layout and emission.
void ApplyResourceSpecialization(Program& program, const ResourceSpecialization& specialization);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_RESOURCEMATERIALIZATION_H_ */
