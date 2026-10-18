#ifndef EMULATOR_SRC_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_SRTNATIVE_H_
#define EMULATOR_SRC_GRAPHICS_SHADER_RECOMPILER_IR_PASSES_SRTNATIVE_H_
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include <cstdint>
#include <memory>
#include <vector>
namespace Libs::Graphics::ShaderRecompiler::IR {
class SrtWalker;
// Senaxx native routine convention: R12 frame, R13 dense memo, R14 generation.
struct SrtNativeFrame {
	ResourcePlan::EvaluationContext::Entry* memo = nullptr;
	uint64_t generation = 0;
	const uint32_t* user_data = nullptr;
	uint64_t user_data_size = 0;
	uint64_t shader_base = 0;
	SrtWalker* walker = nullptr;
	SrtNativeFrame* clean = nullptr;
	const Inst** failed = nullptr;
};
enum class SrtNativeMode : uint32_t { Self, Split };
struct SrtNativeStats { uint32_t plans=0, failed=0; uint64_t bytes=0, instructions=0, interpreted=0; };
bool TakeSrtNativeReport(SrtNativeStats& stats);
class SrtNativeCode {
public:
	~SrtNativeCode();
	SrtNativeCode(const SrtNativeCode&) = delete;
	SrtNativeCode& operator=(const SrtNativeCode&) = delete;
	static std::unique_ptr<SrtNativeCode> Compile(const ResourcePlan& program);
	static bool Supported();
	bool Has(SrtNativeMode mode, uint32_t index) const;
	bool Evaluate(SrtNativeFrame& frame, SrtNativeMode mode, uint32_t index, uint64_t& result) const;
	size_t CodeSize() const { return m_size; }
	uint32_t Instructions() const { return m_instructions; }
	uint32_t Interpreted() const { return m_interpreted; }
private:
	SrtNativeCode() = default;
	using Entry = bool (*)(SrtNativeFrame*, const void*, uint64_t*);
	uint8_t* m_memory = nullptr;
	size_t m_size = 0;
	Entry m_entry = nullptr;
	std::vector<uint32_t> m_routines[2];
	uint32_t m_instructions=0, m_interpreted=0;
};
} // namespace Libs::Graphics::ShaderRecompiler::IR
#endif
