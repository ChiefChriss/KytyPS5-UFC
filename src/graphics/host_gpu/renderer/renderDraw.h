#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_

#include <cstdint>
#include <utility>

namespace Libs::Graphics {

struct ShaderVertexInputInfo;

[[nodiscard]] std::pair<int32_t, uint32_t>
ResolveDrawOffsets(uint32_t index_offset, const ShaderVertexInputInfo& vs_input_info);

// No-location value of an indirect-draw packet's BASE_VTX_LOC / START_INST_LOC field.
inline constexpr uint32_t IndirectNoOffsetLoc = 0x280u;

// User-data slot named by an indirect-draw packet SGPR location (a SH register offset of a
// stage's SPI_SHADER_USER_DATA_*_0 window plus the slot): -1 for no location, -2 when the
// register is outside the known user-data windows.
[[nodiscard]] int32_t IndirectOffsetLocSlot(uint32_t loc);

// Compares a packet location with the vertex shader's embedded-fetch offset SGPR (absolute
// SGPR, -1 when none). Returns 0 same user-data slot (the CP writes the value the shader
// adds, which is what the host vertex/instance offset reproduces), 1 neither side uses a
// slot, 2 different slots, 3 no embedded fetch.
[[nodiscard]] uint32_t ClassifyIndirectOffsetLoc(uint32_t loc, int32_t shader_sgpr,
                                                 uint32_t user_data_base, bool fetch_embedded);

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_RENDERDRAW_H_
