#ifndef EMULATOR_SRC_GRAPHICS_SHADER_RECOMPILER_BACKEND_SPIRV_SPIRVWAVELOWERING_H_
#define EMULATOR_SRC_GRAPHICS_SHADER_RECOMPILER_BACKEND_SPIRV_SPIRVWAVELOWERING_H_

#include "graphics/shader/recompiler/frontend/cfg/ShaderCFG.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <cstddef>
#include <cstdint>

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {

struct EmitterState;
struct ValueEmitContext;

enum class WaveLoweringKind : uint8_t {
	// One guest wave32 per host subgroup of at least 32 lanes.
	NativeWave32,
	// One guest wave64 per host subgroup of 64 lanes.
	NativeWave64,
	// A guest wave64 compute/mesh workgroup on a 32-lane host subgroup: every host invocation
	// carries guest lanes i and i + 32, emitted as two ValueEmitContexts (half 0 and half 1).
	EmulatedWave64,
};

// The single place that decides how guest wave operations map onto host subgroups. Each
// implementation emits exactly what the per-site wave-size checks emitted before; selecting a
// lowering is the only wave-mode decision the SPIR-V backend makes.
class WaveLowering {
public:
	virtual ~WaveLowering() = default;

	// `host_subgroup_size` is the workgroup's host subgroup size, or 0 for stages without one.
	[[nodiscard]] static const WaveLowering& Select(uint32_t guest_wave_size,
	                                                uint32_t host_subgroup_size);
	// Whether ScalarBranchCondition reads a subgroup ballot, for capability analysis that runs
	// before a lowering is selected.
	[[nodiscard]] static bool ScalarBranchNeedsBallot(const IR::Program&   program,
	                                                  CFG::BranchCondition kind);
	[[nodiscard]] static bool IsMaskBranch(CFG::BranchCondition kind);

	[[nodiscard]] virtual WaveLoweringKind Kind() const          = 0;
	[[nodiscard]] virtual const char*      Name() const          = 0;
	[[nodiscard]] virtual uint32_t         GuestWaveSize() const = 0;
	[[nodiscard]] virtual uint32_t         HostHalves() const    = 0;
	[[nodiscard]] bool                     EmulatesHalves() const { return HostHalves() == 2; }

	// Ballot of `predicate` as a uvec4; words 0 and 1 hold guest lanes 0-31 and 32-63.
	virtual uint32_t Ballot(ValueEmitContext& ctx, IR::Value predicate) const = 0;
	// Lowest guest lane set in `ballot` (FindLSB semantics for an empty ballot).
	virtual uint32_t FirstLane(ValueEmitContext& ctx, uint32_t ballot) const = 0;
	// Operand `index` of `inst` as seen by guest lane `lane`.
	virtual uint32_t Shuffle(ValueEmitContext& ctx, const IR::Inst& inst, size_t index,
	                         uint32_t lane) const = 0;
	// The ballot word holding guest lane `lane`; `low` is word 0, already extracted.
	virtual uint32_t BallotWordForLane(EmitterState& state, uint32_t ballot, uint32_t lane,
	                                   uint32_t low) const = 0;
	// A guest scalar branch condition, decided once for the entire guest wave.
	virtual uint32_t ScalarBranchCondition(ValueEmitContext& ctx, const IR::Inst& inst) const = 0;
};

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter

#endif // EMULATOR_SRC_GRAPHICS_SHADER_RECOMPILER_BACKEND_SPIRV_SPIRVWAVELOWERING_H_
