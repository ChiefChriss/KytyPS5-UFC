#include "graphics/shader/recompiler/backend/spirv/spirvWaveLowering.h"

#include "graphics/shader/recompiler/backend/spirv/spirvEmitterInstructions.h"

namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter {
namespace {

// One guest wave per host subgroup: subgroup operations map one to one.
class NativeWaveLowering: public WaveLowering {
public:
	[[nodiscard]] uint32_t HostHalves() const final { return 1; }

	uint32_t Ballot(ValueEmitContext& ctx, IR::Value predicate) const final {
		auto&      state       = ctx.state;
		const auto ballot_type = TypeU32Vector(state, 4);
		const auto scope       = ConstantU32(state, spv::ScopeSubgroup);
		const auto low         = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformBallot, ballot_type, low, scope,
		                          ctx.Def(predicate));
		return low;
	}

	uint32_t FirstLane(ValueEmitContext& ctx, uint32_t ballot) const final {
		auto&      state  = ctx.state;
		const auto result = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformBallotFindLSB, TypeU32(state), result,
		                          ConstantU32(state, spv::ScopeSubgroup), ballot);
		return result;
	}

	uint32_t Shuffle(ValueEmitContext& ctx, const IR::Inst& inst, size_t index,
	                 uint32_t lane) const final {
		auto&      state = ctx.state;
		const auto type  = TypeId(state, inst.Arg(index).GetType());
		const auto scope = ConstantU32(state, spv::ScopeSubgroup);
		const auto low   = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformShuffle, type, low, scope,
		                          ctx.Arg(inst, index), lane);
		return low;
	}
};

class NativeWave32Lowering final: public NativeWaveLowering {
public:
	[[nodiscard]] WaveLoweringKind Kind() const override { return WaveLoweringKind::NativeWave32; }
	[[nodiscard]] const char*      Name() const override { return "native-wave32"; }
	[[nodiscard]] uint32_t         GuestWaveSize() const override { return 32; }

	uint32_t BallotWordForLane(EmitterState& /*state*/, uint32_t /*ballot*/, uint32_t /*lane*/,
	                           uint32_t low) const override {
		return low;
	}

	uint32_t ScalarBranchCondition(ValueEmitContext& ctx, const IR::Inst& inst) const override {
		const auto kind = inst.Flags<CFG::BranchCondition>();
		const bool mask_zero =
		    kind == CFG::BranchCondition::ExecZero || kind == CFG::BranchCondition::VccZero;
		const bool mask_nonzero =
		    kind == CFG::BranchCondition::ExecNonZero || kind == CFG::BranchCondition::VccNonZero;
		if (ctx.state.program.stage != ShaderType::Compute || (!mask_zero && !mask_nonzero))
			return ctx.Arg(inst, 0);
		// Guest scalar mask branches choose once for the entire wave, including lanes with
		// EXEC cleared. A lane-local branch prematurely drops those lanes out of loops.
		const auto ballot = ctx.Ballot(inst.Arg(0));
		const auto bits   = ctx.state.builder.AllocateId();
		const auto result = ctx.state.builder.AllocateId();
		ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), bits, ballot, 0);
		if (mask_zero) {
			const auto active      = ctx.Ballot(IR::Value(true));
			const auto active_bits = ctx.state.builder.AllocateId();
			ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), active_bits,
			                              active, 0);
			ctx.state.builder.AddFunction(spv::OpIEqual, TypeBool(ctx.state), result, bits,
			                              active_bits);
		} else {
			ctx.state.builder.AddFunction(spv::OpINotEqual, TypeBool(ctx.state), result, bits,
			                              ConstantU32(ctx.state, 0));
		}
		return result;
	}
};

uint32_t SelectBallotHighWord(EmitterState& state, uint32_t active_ballot, uint32_t lane,
                              uint32_t low) {
	const auto high     = state.builder.AllocateId();
	const auto in_high  = state.builder.AllocateId();
	const auto selected = state.builder.AllocateId();
	state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), high, active_ballot, 1);
	state.builder.AddFunction(spv::OpUGreaterThanEqual, TypeBool(state), in_high, lane,
	                          ConstantU32(state, 32));
	state.builder.AddFunction(spv::OpSelect, TypeU32(state), selected, in_high, high, low);
	return selected;
}

class NativeWave64Lowering final: public NativeWaveLowering {
public:
	[[nodiscard]] WaveLoweringKind Kind() const override { return WaveLoweringKind::NativeWave64; }
	[[nodiscard]] const char*      Name() const override { return "native-wave64"; }
	[[nodiscard]] uint32_t         GuestWaveSize() const override { return 64; }

	uint32_t BallotWordForLane(EmitterState& state, uint32_t ballot, uint32_t lane,
	                           uint32_t low) const override {
		return SelectBallotHighWord(state, ballot, lane, low);
	}

	// A 64-lane host subgroup already makes scalar branches uniform for the whole wave.
	uint32_t ScalarBranchCondition(ValueEmitContext& ctx, const IR::Inst& inst) const override {
		return ctx.Arg(inst, 0);
	}
};

class EmulatedWave64Lowering final: public WaveLowering {
public:
	[[nodiscard]] WaveLoweringKind Kind() const override {
		return WaveLoweringKind::EmulatedWave64;
	}
	[[nodiscard]] const char* Name() const override { return "emulated-wave64"; }
	[[nodiscard]] uint32_t    GuestWaveSize() const override { return 64; }
	[[nodiscard]] uint32_t    HostHalves() const override { return 2; }

	uint32_t Ballot(ValueEmitContext& ctx, IR::Value predicate) const override {
		auto&      state       = ctx.state;
		auto*      other_half  = ctx.other_half;
		const auto half        = ctx.half;
		const auto ballot_type = TypeU32Vector(state, 4);
		const auto scope       = ConstantU32(state, spv::ScopeSubgroup);
		const auto low         = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformBallot, ballot_type, low, scope,
		                          half == 0 ? ctx.Def(predicate) : other_half->Def(predicate));
		const auto high      = state.builder.AllocateId();
		const auto low_word  = state.builder.AllocateId();
		const auto high_word = state.builder.AllocateId();
		const auto ballot    = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformBallot, ballot_type, high, scope,
		                          half == 1 ? ctx.Def(predicate) : other_half->Def(predicate));
		state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), low_word, low, 0);
		state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), high_word, high, 0);
		state.builder.AddFunction(spv::OpCompositeConstruct, ballot_type, ballot, low_word, high_word,
		                          ConstantU32(state, 0), ConstantU32(state, 0));
		return ballot;
	}

	uint32_t FirstLane(ValueEmitContext& ctx, uint32_t ballot) const override {
		auto&      state      = ctx.state;
		const auto low        = state.builder.AllocateId();
		const auto high       = state.builder.AllocateId();
		const auto low_first  = state.builder.AllocateId();
		const auto high_first = state.builder.AllocateId();
		const auto low_active = state.builder.AllocateId();
		const auto result     = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), low, ballot, 0);
		state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(state), high, ballot, 1);
		state.builder.AddFunction(spv::OpExtInst, TypeU32(state), low_first, GlslStd450(state),
		                          GLSLstd450FindILsb, low);
		state.builder.AddFunction(spv::OpExtInst, TypeU32(state), high_first, GlslStd450(state),
		                          GLSLstd450FindILsb, high);
		state.builder.AddFunction(spv::OpINotEqual, TypeBool(state), low_active, low,
		                          ConstantU32(state, 0));
		state.builder.AddFunction(spv::OpSelect, TypeU32(state), result, low_active, low_first,
		                          EmitAddU32(state, high_first, ConstantU32(state, 32)));
		return result;
	}

	uint32_t Shuffle(ValueEmitContext& ctx, const IR::Inst& inst, size_t index,
	                 uint32_t lane) const override {
		auto&      state = ctx.state;
		const auto type  = TypeId(state, inst.Arg(index).GetType());
		const auto scope = ConstantU32(state, spv::ScopeSubgroup);
		const auto low   = state.builder.AllocateId();
		const auto physical_lane =
		    EmitBinaryU32(state, spv::OpBitwiseAnd, lane, ConstantU32(state, 31));
		const auto high          = state.builder.AllocateId();
		const auto in_high       = state.builder.AllocateId();
		const auto value         = state.builder.AllocateId();
		state.builder.AddFunction(spv::OpGroupNonUniformShuffle, type, low, scope,
		                          ctx.HalfArg(inst, index, 0), physical_lane);
		state.builder.AddFunction(spv::OpGroupNonUniformShuffle, type, high, scope,
		                          ctx.HalfArg(inst, index, 1), physical_lane);
		state.builder.AddFunction(spv::OpINotEqual, TypeBool(state), in_high,
		                          EmitBinaryU32(state, spv::OpBitwiseAnd, lane, ConstantU32(state, 32)),
		                          ConstantU32(state, 0));
		state.builder.AddFunction(spv::OpSelect, type, value, in_high, high, low);
		return value;
	}

	uint32_t BallotWordForLane(EmitterState& state, uint32_t ballot, uint32_t lane,
	                           uint32_t low) const override {
		return SelectBallotHighWord(state, ballot, lane, low);
	}

	uint32_t ScalarBranchCondition(ValueEmitContext& ctx, const IR::Inst& inst) const override {
		const auto kind = inst.Flags<CFG::BranchCondition>();
		// A native scalar branch makes one decision for both emulated wave halves.
		if (ctx.half != 0) return ctx.other_half->Def(IR::Value(&inst));
		if (kind == CFG::BranchCondition::ScalarInstruction) return ctx.Arg(inst, 0);
		const auto ballot   = ctx.Ballot(inst.Arg(0));
		const auto low      = ctx.state.builder.AllocateId();
		const auto high     = ctx.state.builder.AllocateId();
		const auto combined = ctx.state.builder.AllocateId();
		const auto result   = ctx.state.builder.AllocateId();
		ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), low, ballot, 0);
		ctx.state.builder.AddFunction(spv::OpCompositeExtract, TypeU32(ctx.state), high, ballot, 1);
		const bool zero = kind == CFG::BranchCondition::ExecZero ||
		                  kind == CFG::BranchCondition::VccZero ||
		                  kind == CFG::BranchCondition::SccZero;
		ctx.state.builder.AddFunction(zero ? spv::OpBitwiseAnd : spv::OpBitwiseOr, TypeU32(ctx.state),
		                              combined, low, high);
		ctx.state.builder.AddFunction(zero ? spv::OpIEqual : spv::OpINotEqual, TypeBool(ctx.state),
		                              result, combined, ConstantU32(ctx.state, zero ? ~0u : 0u));
		return result;
	}
};

} // namespace

const WaveLowering& WaveLowering::Select(uint32_t guest_wave_size, uint32_t host_subgroup_size) {
	static const NativeWave32Lowering   native_wave32;
	static const NativeWave64Lowering   native_wave64;
	static const EmulatedWave64Lowering emulated_wave64;
	if (guest_wave_size == 64u && host_subgroup_size == 32u) {
		return emulated_wave64;
	}
	return guest_wave_size == 32u ? static_cast<const WaveLowering&>(native_wave32)
	                              : static_cast<const WaveLowering&>(native_wave64);
}

bool WaveLowering::IsMaskBranch(CFG::BranchCondition kind) {
	return kind == CFG::BranchCondition::ExecZero || kind == CFG::BranchCondition::ExecNonZero ||
	       kind == CFG::BranchCondition::VccZero || kind == CFG::BranchCondition::VccNonZero;
}

bool WaveLowering::ScalarBranchNeedsBallot(const IR::Program& program, CFG::BranchCondition kind) {
	return program.stage == ShaderType::Compute && program.wave_size == 32 && IsMaskBranch(kind);
}

} // namespace Libs::Graphics::ShaderRecompiler::Spirv::Emitter
