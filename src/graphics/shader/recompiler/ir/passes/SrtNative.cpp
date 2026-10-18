#include "graphics/shader/recompiler/ir/passes/SrtNative.h"
#include "common/assert.h"
#include "common/config.h"
#include "graphics/shader/recompiler/ir/passes/SrtNativeAsm.h"
#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"
#include <bit>
#include <cstddef>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {
using namespace X64;
constexpr int32_t FrameMemo=0, FrameGeneration=8, FrameUserData=16, FrameUserDataSize=24,
                  FrameShaderBase=32, FrameClean=48, FrameFailed=56;
static_assert(offsetof(SrtNativeFrame,memo)==FrameMemo);
static_assert(offsetof(SrtNativeFrame,generation)==FrameGeneration);
static_assert(offsetof(SrtNativeFrame,user_data)==FrameUserData);
static_assert(offsetof(SrtNativeFrame,user_data_size)==FrameUserDataSize);
static_assert(offsetof(SrtNativeFrame,shader_base)==FrameShaderBase);
static_assert(offsetof(SrtNativeFrame,clean)==FrameClean);
static_assert(offsetof(SrtNativeFrame,failed)==FrameFailed);
static_assert(sizeof(ResourcePlan::EvaluationContext::Entry)==16);
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
constexpr Reg Arg0=RCX, Arg1=RDX, Arg2=R8;
constexpr int32_t Shadow=32;
#else
constexpr Reg Arg0=RDI, Arg1=RSI, Arg2=RDX;
constexpr int32_t Shadow=0;
#endif
bool ImmediateBits(Value value, uint64_t& bits) {
	switch(value.GetType()) {
		case Type::U1: bits=value.U1(); return true;
		case Type::U8: bits=value.U8(); return true;
		case Type::U16: bits=value.U16(); return true;
		case Type::U32: bits=value.U32(); return true;
		case Type::U64: bits=value.U64(); return true;
		case Type::F32: bits=std::bit_cast<uint32_t>(value.F32Value()); return true;
		default: return false;
	}
}
} // namespace

// Senaxx 886f53db/dcc44b76. The helpers retain OUR raw-read and SGPR-read-mask semantics:
// native arithmetic must not adopt unrelated scalar OOB/null-base behavior from the fork.
struct SrtNativeHelpers {
	static bool Interpret(SrtNativeFrame* frame, const Inst* inst, uint64_t* result) {
		return frame->walker->EvaluateInst(*inst,*result);
	}
	static bool EvaluateValue(SrtNativeFrame* frame, const Inst* inst, uint64_t* result) {
		return frame->walker->EvaluateWide(Value(const_cast<Inst*>(inst)),*result);
	}
};

namespace {
class Compiler {
public:
	explicit Compiler(const ResourcePlan& program): m_program(program) {}
	bool Run(std::vector<uint32_t> (&routines)[2], uint32_t& instructions, uint32_t& interpreted) {
		for(const auto& inst:m_program.value_storage) {
			const auto index=inst.EvaluationIndex(m_program.evaluation_value_count);
			if(index>=m_insts.size()) m_insts.resize(index+1,nullptr);
			m_insts[index]=&inst;
		}
		// Bound code displacements/memo offsets; fallback is always available.
		if(m_insts.size()>static_cast<size_t>(INT32_MAX/16)) return false;
		for(auto& labels:m_labels) {
			labels.resize(m_insts.size());
			for(auto& label:labels) label=m_asm.NewLabel();
		}
		EmitEntry();
		for(uint32_t mode=0;mode<2;mode++) {
			routines[mode].assign(m_insts.size(),UINT32_MAX);
			for(size_t i=0;i<m_insts.size();i++) {
				if(!m_insts[i]) continue;
				if(m_asm.Size()>UINT32_MAX) return false;
				routines[mode][i]=static_cast<uint32_t>(m_asm.Size());
				EmitRoutine(static_cast<SrtNativeMode>(mode),*m_insts[i]);
			}
		}
		instructions=static_cast<uint32_t>(m_insts.size());
		interpreted=m_interpreted/2;
		return m_asm.Finish();
	}
	const Assembler& Asm() const { return m_asm; }
private:
	void EmitEntry() {
		auto& a=m_asm;
		a.Push(RBX); a.Push(RBP); a.Push(R12); a.Push(R13); a.Push(R14); a.Push(R15);
		a.MovRR(R15,Arg2); a.MovRR(R11,Arg1); a.MovRR(R12,Arg0);
		a.Load(R13,R12,FrameMemo); a.Load(R14,R12,FrameGeneration); a.CallReg(R11);
		const auto failed=a.NewLabel(),done=a.NewLabel();
		a.Jcc(CondB,failed); a.Store(R15,0,RAX); a.MovRI(RAX,1); a.Jmp(done);
		a.Bind(failed); a.MovRI(RAX,0); a.Bind(done);
		a.Pop(R15); a.Pop(R14); a.Pop(R13); a.Pop(R12); a.Pop(RBP); a.Pop(RBX); a.Ret();
	}
	bool Compiled(const Inst* inst) const {
		if(!inst) return false;
		const auto index=inst->EvaluationIndex(m_program.evaluation_value_count);
		return index<m_insts.size() && m_insts[index]==inst;
	}
	Label RoutineLabel(SrtNativeMode mode,const Inst* inst) const {
		return m_labels[static_cast<uint32_t>(mode)][inst->EvaluationIndex(m_program.evaluation_value_count)];
	}
	template<typename Setup> void EmitHelperCall(const void* helper,Setup&& setup) {
		auto& a=m_asm;
		a.Push(RBX); a.MovRR(RBX,RSP); a.AndI(RSP,-16); a.SubI(RSP,Shadow+16);
		setup(); a.CallAbs(helper); a.MovzxB(RCX,RAX); a.Load(RAX,RSP,Shadow);
		a.MovRR(RSP,RBX); a.Pop(RBX); a.Load(R13,R12,FrameMemo);
	}
	void EmitValue(SrtNativeMode mode,Value value,Label fail) {
		auto& a=m_asm; value=value.Resolve();
		if(value.IsImmediate()) { uint64_t bits=0; if(ImmediateBits(value,bits)) a.MovRI(RAX,bits); else a.Jmp(fail); return; }
		const auto* inst=value.TryInstruction();
		if(!inst) { a.Jmp(fail); return; }
		if(!Compiled(inst)) {
			EmitHelperCall(reinterpret_cast<const void*>(&SrtNativeHelpers::EvaluateValue),[&] {
				a.MovRR(Arg0,R12); a.MovRI(Arg1,reinterpret_cast<uint64_t>(inst)); a.Lea(Arg2,RSP,Shadow);
			});
			a.Test(RCX,RCX); a.Jcc(CondE,fail); return;
		}
		a.Call(RoutineLabel(mode,inst)); a.Jcc(CondB,fail);
	}
	void EmitCleanValue(SrtNativeMode mode,Value value,Label fail) {
		if(mode==SrtNativeMode::Self || value.Resolve().IsImmediate()) { EmitValue(SrtNativeMode::Self,value,fail); return; }
		auto& a=m_asm;
		a.Push(R12); a.Push(R13); a.Push(R14);
		a.Load(R12,R12,FrameClean); a.Load(R13,R12,FrameMemo); a.Load(R14,R12,FrameGeneration);
		const auto failed=a.NewLabel(),done=a.NewLabel();
		EmitValue(SrtNativeMode::Self,value,failed);
		a.Pop(R14); a.Pop(R13); a.Pop(R12); a.Jmp(done);
		a.Bind(failed); a.Pop(R14); a.Pop(R13); a.Pop(R12); a.Jmp(fail); a.Bind(done);
	}
	void EmitOperands(SrtNativeMode mode,const Inst& inst,size_t count,Label fail) {
		static constexpr Reg Targets[4]={RAX,RCX,RDX,R8};
		auto& a=m_asm;
		uint64_t bits[4]{}; bool immediate[4]{};
		for(size_t i=0;i<count;i++) {
			const auto value=inst.Arg(i).Resolve();
			if(value.IsImmediate()) { if(!ImmediateBits(value,bits[i])) { a.Jmp(fail); return; } immediate[i]=true; }
			else { EmitValue(mode,value,fail); a.Push(RAX); }
		}
		for(size_t i=count;i-->0;) if(!immediate[i]) a.Pop(Targets[i]);
		for(size_t i=0;i<count;i++) if(immediate[i]) a.MovRI(Targets[i],bits[i]);
	}
	void EmitInterpret(const Inst& inst,Label fail) {
		auto& a=m_asm; m_interpreted++;
		EmitHelperCall(reinterpret_cast<const void*>(&SrtNativeHelpers::Interpret),[&] {
			a.MovRR(Arg0,R12); a.MovRI(Arg1,reinterpret_cast<uint64_t>(&inst)); a.Lea(Arg2,RSP,Shadow);
		});
		a.Test(RCX,RCX); a.Jcc(CondE,fail);
	}
	void EmitCompare(SrtNativeMode mode,const Inst& inst,Cond cond,Label fail) {
		auto& a=m_asm; EmitOperands(mode,inst,2,fail); a.Cmp32(RAX,RCX); a.SetCC(cond,RAX); a.MovzxB(RAX,RAX);
	}
	void EmitBool(Reg reg) { m_asm.Test(reg,reg); m_asm.SetCC(CondNE,reg); m_asm.MovzxB(reg,reg); }
	void EmitFlushDenorm(Reg reg) {
		auto& a=m_asm; const auto keep=a.NewLabel();
		a.MovRR32(R9,reg); a.AndI32(R9,0x7fffffffu); a.CmpI(R9,0x00800000); a.Jcc(CondAE,keep);
		a.AndI32(reg,0x80000000u); a.Bind(keep);
	}
	void EmitExtract(SrtNativeMode mode,const Inst& inst,Label fail) {
		auto& a=m_asm; const auto index=inst.Arg(1).Resolve();
		if(!index.IsImmediate()||index.GetType()!=Type::U32||index.U32()>=2) { a.Jmp(fail); return; }
		const auto component=index.U32();
		if(inst.GetOpcode()==ValueOpcode::CompositeExtractU64) {
			EmitValue(mode,inst.Arg(0),fail); if(component) a.ShrI(RAX,32); a.MovRR32(RAX,RAX); return;
		}
		const auto* source=inst.Arg(0).Resolve().TryInstruction();
		if(!source) { EmitInterpret(inst,fail); return; }
		if(source->GetOpcode()==ValueOpcode::CompositeConstructU32x2) { EmitValue(mode,source->Arg(component),fail); return; }
		if(source->GetOpcode()==ValueOpcode::IAddCarry32) {
			EmitOperands(mode,*source,2,fail); a.MovRR32(RAX,RAX); a.MovRR32(RCX,RCX); a.Add(RAX,RCX);
			if(component) a.ShrI(RAX,32); else a.MovRR32(RAX,RAX); return;
		}
		a.Jmp(fail);
	}
	void EmitBody(SrtNativeMode mode,const Inst& inst,Label fail) {
		auto& a=m_asm;
		switch(inst.GetOpcode()) {
			// SGPR reads use the existing helper to preserve the UFC memo read mask exactly.
			case ValueOpcode::GetUserData: EmitInterpret(inst,fail); return;
			case ValueOpcode::GetShaderBase: a.Load(RAX,R12,FrameShaderBase); return;
			case ValueOpcode::Phi: {
				const auto value=ResolveInvariantPhi(m_program,Value(const_cast<Inst*>(&inst)));
				if(value.IsEmpty()) a.Jmp(fail); else EmitValue(mode,value,fail); return;
			}
			case ValueOpcode::BitCastU32F32: case ValueOpcode::BitCastF32U32: case ValueOpcode::ConditionRef:
				EmitValue(mode,inst.Arg(0),fail); return;
			case ValueOpcode::CompositeExtractU64: case ValueOpcode::CompositeExtractU32x2:
				EmitExtract(mode,inst,fail); return;
			case ValueOpcode::CompositeConstructU64:
				EmitOperands(mode,inst,2,fail); a.MovRR32(RAX,RAX); a.ShlI(RCX,32); a.Or(RAX,RCX); return;
			case ValueOpcode::ReadConst: {
				const auto slot=inst.Arg(1).Resolve();
				if(!slot.IsImmediate()||slot.GetType()!=Type::U32||slot.U32()>=m_program.srt_reads.size()) { a.Jmp(fail); return; }
				const auto index=slot.U32(); const auto value=m_program.srt_reads[index].value;
				if(mode==SrtNativeMode::Split && index<m_program.clean_flat_slots.size() && m_program.clean_flat_slots[index]) EmitCleanValue(mode,value,fail);
				else EmitValue(mode,value,fail);
				return;
			}
			case ValueOpcode::IAdd32: EmitOperands(mode,inst,2,fail); a.Add(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::IAdd64: EmitOperands(mode,inst,2,fail); a.Add(RAX,RCX); return;
			case ValueOpcode::ISub32: EmitOperands(mode,inst,2,fail); a.Sub(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::ISub64: EmitOperands(mode,inst,2,fail); a.Sub(RAX,RCX); return;
			case ValueOpcode::IMul32: EmitOperands(mode,inst,2,fail); a.Imul(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::IMul64: EmitOperands(mode,inst,2,fail); a.Imul(RAX,RCX); return;
			case ValueOpcode::UMin32: {
				EmitOperands(mode,inst,2,fail); const auto done=a.NewLabel();
				a.MovRR32(RAX,RAX); a.MovRR32(RCX,RCX); a.Cmp(RAX,RCX); a.Jcc(CondBE,done); a.MovRR(RAX,RCX); a.Bind(done); return;
			}
			case ValueOpcode::ConvertF32U32:
				EmitValue(mode,inst.Arg(0),fail); a.MovRR32(RAX,RAX); a.Cvtsi2ss64(XMM0,RAX); a.MovdRX(RAX,XMM0); return;
			case ValueOpcode::ConvertU32F32: {
				EmitValue(mode,inst.Arg(0),fail); a.MovRR32(RAX,RAX); a.MovRR(RCX,RAX);
				a.AndI32(RCX,0x7f800000u); a.CmpI(RCX,0x7f800000); a.Jcc(CondE,fail);
				const auto positive=a.NewLabel(); a.MovRR(RCX,RAX); a.ShrI(RCX,31); a.Test(RCX,RCX); a.Jcc(CondE,positive);
				a.MovRR(RCX,RAX); a.AndI32(RCX,0x7fffffffu); a.Test(RCX,RCX); a.Jcc(CondNE,fail); a.Bind(positive);
				a.MovdXR(XMM0,RAX); a.Cvttss2si64(RAX,XMM0); a.MovRI(RCX,UINT32_MAX); a.Cmp(RAX,RCX); a.Jcc(CondA,fail); return;
			}
			case ValueOpcode::FPMul32:
				EmitOperands(mode,inst,2,fail); a.MovdXR(XMM0,RAX); a.MovdXR(XMM1,RCX); a.Mulss(XMM0,XMM1); a.MovdRX(RAX,XMM0); return;
			case ValueOpcode::FPTrunc32:
				EmitValue(mode,inst.Arg(0),fail); a.MovdXR(XMM0,RAX); a.Roundss(XMM0,XMM0,0x0b); a.MovdRX(RAX,XMM0); return;
			case ValueOpcode::FPIsNan32:
				EmitValue(mode,inst.Arg(0),fail); a.AndI32(RAX,0x7fffffffu); a.CmpI(RAX,0x7f800000); a.SetCC(CondA,RAX); a.MovzxB(RAX,RAX); return;
			case ValueOpcode::FPOrdLessThanEqual32: case ValueOpcode::FPOrdGreaterThanEqual32:
				EmitOperands(mode,inst,2,fail);
				if(inst.Flags<FPCompareFlags>().flush_input_denorms) { EmitFlushDenorm(RAX); EmitFlushDenorm(RCX); }
				a.MovdXR(XMM0,RAX); a.MovdXR(XMM1,RCX);
				if(inst.GetOpcode()==ValueOpcode::FPOrdLessThanEqual32) a.Ucomiss(XMM1,XMM0); else a.Ucomiss(XMM0,XMM1);
				a.SetCC(CondAE,RAX); a.MovzxB(RAX,RAX); return;
			case ValueOpcode::BitwiseAnd32: EmitOperands(mode,inst,2,fail); a.And(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::BitwiseAnd64: EmitOperands(mode,inst,2,fail); a.And(RAX,RCX); return;
			case ValueOpcode::BitwiseOr32: EmitOperands(mode,inst,2,fail); a.Or(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::BitwiseXor32: EmitOperands(mode,inst,2,fail); a.Xor(RAX,RCX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::BitwiseNot32: EmitValue(mode,inst.Arg(0),fail); a.Not(RAX); a.MovRR32(RAX,RAX); return;
			case ValueOpcode::ShiftLeftLogical32: EmitOperands(mode,inst,2,fail); a.ShlCl32(RAX); return;
			case ValueOpcode::ShiftLeftLogical64: EmitOperands(mode,inst,2,fail); a.ShlCl(RAX); return;
			case ValueOpcode::ShiftRightLogical32: EmitOperands(mode,inst,2,fail); a.ShrCl32(RAX); return;
			case ValueOpcode::ShiftRightLogical64: EmitOperands(mode,inst,2,fail); a.ShrCl(RAX); return;
			case ValueOpcode::ShiftRightArithmetic32: EmitOperands(mode,inst,2,fail); a.SarCl32(RAX); return;
			case ValueOpcode::ShiftRightArithmetic64: EmitOperands(mode,inst,2,fail); a.SarCl(RAX); return;
			case ValueOpcode::SelectU32: case ValueOpcode::SelectU1: case ValueOpcode::SelectF32: {
				const auto other=a.NewLabel(),done=a.NewLabel(); EmitCleanValue(mode,inst.Arg(0),fail);
				a.Test(RAX,RAX); a.Jcc(CondE,other); EmitValue(mode,inst.Arg(1),fail); a.Jmp(done);
				a.Bind(other); EmitValue(mode,inst.Arg(2),fail); a.Bind(done); return;
			}
			case ValueOpcode::IEqual32: EmitCompare(mode,inst,CondE,fail); return;
			case ValueOpcode::INotEqual32: EmitCompare(mode,inst,CondNE,fail); return;
			case ValueOpcode::ULessThan32: EmitCompare(mode,inst,CondB,fail); return;
			case ValueOpcode::UGreaterThan32: EmitCompare(mode,inst,CondA,fail); return;
			case ValueOpcode::SGreaterThanEqual32: EmitCompare(mode,inst,CondGE,fail); return;
			case ValueOpcode::LogicalAnd: case ValueOpcode::LogicalOr: {
				const bool is_and=inst.GetOpcode()==ValueOpcode::LogicalAnd;
				const auto left_failed=a.NewLabel(),done=a.NewLabel(); EmitValue(mode,inst.Arg(0),left_failed);
				EmitBool(RAX); a.Test(RAX,RAX); a.Jcc(is_and?CondE:CondNE,done);
				EmitValue(mode,inst.Arg(1),fail); EmitBool(RAX); a.Jmp(done);
				a.Bind(left_failed); EmitValue(mode,inst.Arg(1),fail); a.Test(RAX,RAX); a.Jcc(is_and?CondNE:CondE,fail);
				EmitBool(RAX); a.Bind(done); return;
			}
			case ValueOpcode::LogicalXor:
				EmitOperands(mode,inst,2,fail); EmitBool(RAX); EmitBool(RCX); a.Xor(RAX,RCX); return;
			case ValueOpcode::LogicalNot:
				EmitValue(mode,inst.Arg(0),fail); a.Test(RAX,RAX); a.SetCC(CondE,RAX); a.MovzxB(RAX,RAX); return;
			// ReadFirstLane, reads and less frequent bit-field operations mix freely with native
			// operand routines while preserving the production interpreter's exact semantics.
			default: EmitInterpret(inst,fail); return;
		}
	}
	void EmitRoutine(SrtNativeMode mode,const Inst& inst) {
		auto& a=m_asm; const auto index=inst.EvaluationIndex(m_program.evaluation_value_count);
		const auto value=static_cast<int32_t>(index*16u),state=value+8;
		const auto compute=a.NewLabel(),cycle=a.NewLabel(),fail=a.NewLabel();
		a.Bind(RoutineLabel(mode,&inst)); a.Load(RAX,R13,state); a.Cmp(RAX,R14); a.Jcc(CondNE,compute); a.Load(RAX,R13,value); a.Ret();
		a.Bind(compute); a.Lea(RCX,R14,1); a.Cmp(RAX,RCX); a.Jcc(CondE,cycle); a.Store(R13,state,RCX);
		a.Push(RBP); a.MovRR(RBP,RSP); EmitBody(mode,inst,fail);
		a.MovRR(RSP,RBP); a.Pop(RBP); a.Load(R13,R12,FrameMemo); a.Store(R13,value,RAX); a.Store(R13,state,R14); a.Clc(); a.Ret();
		a.Bind(fail); a.MovRR(RSP,RBP); a.Pop(RBP); a.Load(R13,R12,FrameMemo); a.StoreImm32(R13,state,0);
		const auto recorded=a.NewLabel(); a.Load(RAX,R12,FrameFailed); a.CmpMemI(RAX,0,0); a.Jcc(CondNE,recorded);
		a.MovRI(RCX,reinterpret_cast<uint64_t>(&inst)); a.Store(RAX,0,RCX); a.Bind(recorded); a.Stc(); a.Ret();
		a.Bind(cycle); a.Stc(); a.Ret();
	}
	const ResourcePlan& m_program;
	Assembler m_asm;
	std::vector<const Inst*> m_insts;
	std::vector<Label> m_labels[2];
	uint32_t m_interpreted=0;
};
} // namespace

void* SrtNativeAllocateCode(const void*,size_t);
void SrtNativeFreeCode(void*,size_t);
SrtNativeCode::~SrtNativeCode() { SrtNativeFreeCode(m_memory,m_size); }
bool SrtNativeCode::Supported() {
#if defined(__x86_64__) || defined(_M_X64)
	return true;
#else
	return false;
#endif
}
std::unique_ptr<SrtNativeCode> SrtNativeCode::Compile(const ResourcePlan& program) {
	if(!Supported()) return nullptr;
	std::unique_ptr<SrtNativeCode> code(new SrtNativeCode()); Compiler compiler(program);
	if(!compiler.Run(code->m_routines,code->m_instructions,code->m_interpreted)) return nullptr;
	const auto& bytes=compiler.Asm().Code(); code->m_size=bytes.size();
	code->m_memory=static_cast<uint8_t*>(SrtNativeAllocateCode(bytes.data(),bytes.size()));
	if(!code->m_memory) return nullptr;
	code->m_entry=reinterpret_cast<Entry>(code->m_memory); return code;
}
bool SrtNativeCode::Has(SrtNativeMode mode,uint32_t index) const {
	const auto& routines=m_routines[static_cast<uint32_t>(mode)];
	return index<routines.size() && routines[index]!=UINT32_MAX;
}
bool SrtNativeCode::Evaluate(SrtNativeFrame& frame,SrtNativeMode mode,uint32_t index,uint64_t& result) const {
	EXIT_IF(!Has(mode,index));
	return m_entry(&frame,m_memory+m_routines[static_cast<uint32_t>(mode)][index],&result);
}
} // namespace Libs::Graphics::ShaderRecompiler::IR
