#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_RENDERER_GPUCRASHDIAGNOSTICS_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_RENDERER_GPUCRASHDIAGNOSTICS_H_

#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>

namespace Libs::Graphics {

struct GraphicContext;

namespace GpuCrashDiagnostics {

struct CheckpointInfo {
	uint32_t op          = 0;
	uint64_t submit_id   = 0;
	uint32_t args[4]     = {};
	uint64_t arg4        = 0;
	uint64_t ps_addr     = 0;
	uint64_t es_addr     = 0;
	uint64_t gs_addr     = 0;
	uint64_t shader_hash = 0;
};

// Attaches the compiled shader hash to the most recent checkpoint on this thread.
void AnnotateLastCheckpoint(uint64_t shader_hash);

// Records a vkCmdSetCheckpointNV marker and/or top- and bottom-of-pipe buffer markers;
// no-op unless checkpoints or breadcrumbs are enabled.
void RecordCheckpoint(const GraphicContext& graphics, vk::CommandBuffer buffer,
                      const CheckpointInfo& info);

// Copies the indirect dispatch arguments the GPU is about to consume into a host-visible ring
// tagged with the current checkpoint, so a hang can be matched to its workgroup counts.
void RecordIndirectArgs(const GraphicContext& graphics, vk::CommandBuffer buffer,
                        vk::Buffer args_buffer, uint64_t args_offset);

// Diagnostic-only bounded snapshot of the suspect compute shader's SSBO binding 2.
void RecordLoopCountBuffer(const GraphicContext& graphics, vk::CommandBuffer command,
                           const vk::DescriptorBufferInfo& source, uint64_t guest_address);

// Logs the in-flight breadcrumb range, the last checkpoints reached per pipeline stage and
// VK_EXT_device_fault data.
// Only the first call after a device loss reports.
void ReportDeviceLost(const GraphicContext& graphics);

} // namespace GpuCrashDiagnostics

} // namespace Libs::Graphics

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_RENDERER_GPUCRASHDIAGNOSTICS_H_ */
