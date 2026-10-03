#include "graphics/host_gpu/renderer/gpuCrashDiagnostics.h"

#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/render.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fmt/format.h>
#include <mutex>
#include <string>
#include <vector>

namespace Libs::Graphics::GpuCrashDiagnostics {

namespace {

constexpr uint64_t RING_SIZE = 1u << 16u;

struct Record {
	std::atomic<uint64_t> sequence {0};
	CheckpointInfo        info;
};

std::array<Record, RING_SIZE> g_ring;
std::atomic<uint64_t>         g_next_sequence {1};
std::atomic_flag              g_reported      = ATOMIC_FLAG_INIT;
thread_local uint64_t         t_last_sequence = 0;

struct IndirectArgsSlot {
	uint32_t x;
	uint32_t y;
	uint32_t z;
	uint32_t sequence;
};

constexpr uint32_t ARGS_SLOTS = 256;

std::once_flag          g_args_once;
vk::Buffer              g_args_buffer = nullptr;
vk::DeviceMemory        g_args_memory = nullptr;
const IndirectArgsSlot* g_args_slots  = nullptr;
std::atomic<uint32_t>   g_args_next {0};

constexpr uint64_t SNAPSHOT_LIMIT  = 1024 * 1024;
constexpr uint64_t SNAPSHOT_STRIDE = SNAPSHOT_LIMIT + 64;
constexpr uint32_t SNAPSHOT_SLOTS  = 8;
struct SnapshotMetadata {
	uint64_t sequence;
	uint64_t bytes;
	uint64_t guest_address;
	uint64_t source_offset;
};
// Breadcrumb markers hold the low 32 bits of a sequence: [0] is written at top-of-pipe and
// [1] at bottom-of-pipe, both recorded before the command's own work.
constexpr uint32_t       MARKER_STARTED   = 0;
constexpr uint32_t       MARKER_COMPLETED = 1;
constexpr uint64_t       HEARTBEAT_STRIDE = 1u << 14u;
constexpr auto           HEARTBEAT_PERIOD = std::chrono::seconds(30);
std::once_flag           g_marker_once;
vk::Buffer               g_marker_buffer = nullptr;
vk::DeviceMemory         g_marker_memory = nullptr;
const volatile uint32_t* g_markers       = nullptr;
std::atomic<int64_t>     g_heartbeat_ns {0};

vk::Buffer       g_snapshot_buffer    = nullptr;
vk::DeviceMemory g_snapshot_memory    = nullptr;
const uint32_t*  g_snapshot_words     = nullptr;
uint32_t         g_snapshot_next      = 0;
bool             g_snapshot_attempted = false;

void ReportLoopCountBuffer() {
	if (g_snapshot_words == nullptr) return;
	for (uint32_t slot = 0; slot < SNAPSHOT_SLOTS; ++slot) {
		const auto*      words = g_snapshot_words + slot * SNAPSHOT_STRIDE / sizeof(uint32_t);
		SnapshotMetadata metadata {};
		std::memcpy(&metadata, words + SNAPSHOT_LIMIT / sizeof(uint32_t), sizeof(metadata));
		if (metadata.sequence == 0 || metadata.bytes > SNAPSHOT_LIMIT) continue;
		uint32_t maximum       = 0;
		uint64_t maximum_index = 0, nonzero = 0, over256 = 0;
		for (uint64_t i = 0; i < metadata.bytes / sizeof(uint32_t); ++i) {
			const auto count = words[i] >> 16;
			if (count > maximum) {
				maximum       = count;
				maximum_index = i;
			}
			nonzero += count != 0;
			over256 += count > 256;
		}
		Log::WriteToConsoleAndLog(fmt::format(
		    "GPU loop-count snapshot: seq={} bytes={} max_high16={} at_word={} nonzero={} "
		    "over256={} "
		    "(whole captured range, not just shader-indexed headers)\n",
		    metadata.sequence, metadata.bytes, maximum, maximum_index, nonzero, over256));
		Log::WriteToConsoleAndLog(fmt::format("  guest=0x{:012x} source_offset={}\n",
		                                      metadata.guest_address, metadata.source_offset));
		for (uint64_t i = 0; i < std::min<uint64_t>(metadata.bytes / sizeof(uint32_t), 32); ++i) {
			Log::WriteToConsoleAndLog(fmt::format("  binding2 word[{}]=0x{:08x} high16={}\n", i,
			                                      words[i], words[i] >> 16));
		}
	}
}

void CreateArgsRing(const GraphicContext& graphics) {
	vk::BufferCreateInfo buffer_info {};
	buffer_info.size  = sizeof(IndirectArgsSlot) * ARGS_SLOTS;
	buffer_info.usage = vk::BufferUsageFlagBits::eTransferDst;
	if (graphics.device.createBuffer(&buffer_info, nullptr, &g_args_buffer) !=
	    vk::Result::eSuccess) {
		g_args_buffer = nullptr;
		return;
	}
	const auto requirements = graphics.device.getBufferMemoryRequirements(g_args_buffer);
	const auto wanted =
	    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
	const auto& memory = graphics.GetPhysicalDeviceMemoryProperties();
	for (uint32_t type = 0; type < memory.memoryTypeCount; type++) {
		if ((requirements.memoryTypeBits & (1u << type)) == 0 ||
		    (memory.memoryTypes[type].propertyFlags & wanted) != wanted) {
			continue;
		}
		vk::MemoryAllocateInfo allocate {};
		allocate.allocationSize  = requirements.size;
		allocate.memoryTypeIndex = type;
		void* mapped             = nullptr;
		if (graphics.device.allocateMemory(&allocate, nullptr, &g_args_memory) ==
		        vk::Result::eSuccess &&
		    graphics.device.bindBufferMemory(g_args_buffer, g_args_memory, 0) ==
		        vk::Result::eSuccess &&
		    graphics.device.mapMemory(g_args_memory, 0, VK_WHOLE_SIZE, {}, &mapped) ==
		        vk::Result::eSuccess) {
			std::memset(mapped, 0, buffer_info.size);
			g_args_slots = static_cast<const IndirectArgsSlot*>(mapped);
		}
		return;
	}
}

void CreateMarkerBuffer(const GraphicContext& graphics) {
	vk::BufferCreateInfo buffer_info {};
	buffer_info.size  = sizeof(uint32_t) * 2;
	buffer_info.usage = vk::BufferUsageFlagBits::eTransferDst;
	if (graphics.device.createBuffer(&buffer_info, nullptr, &g_marker_buffer) !=
	    vk::Result::eSuccess) {
		g_marker_buffer = nullptr;
		Log::WriteToConsoleAndLog("GPU breadcrumbs: marker buffer creation failed\n");
		return;
	}
	const auto requirements = graphics.device.getBufferMemoryRequirements(g_marker_buffer);
	const auto wanted =
	    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
	const auto& memory = graphics.GetPhysicalDeviceMemoryProperties();
	// System memory stays readable after a driver reset; device-local host-visible memory may not.
	int32_t chosen = -1;
	for (uint32_t type = 0; type < memory.memoryTypeCount; type++) {
		const auto flags = memory.memoryTypes[type].propertyFlags;
		if ((requirements.memoryTypeBits & (1u << type)) == 0 || (flags & wanted) != wanted) {
			continue;
		}
		if (chosen < 0 || !(flags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
			chosen = static_cast<int32_t>(type);
			if (!(flags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
				break;
			}
		}
	}
	vk::MemoryAllocateInfo allocate {};
	allocate.allocationSize  = requirements.size;
	allocate.memoryTypeIndex = static_cast<uint32_t>(chosen);
	void* mapped             = nullptr;
	if (chosen < 0 ||
	    graphics.device.allocateMemory(&allocate, nullptr, &g_marker_memory) !=
	        vk::Result::eSuccess ||
	    graphics.device.bindBufferMemory(g_marker_buffer, g_marker_memory, 0) !=
	        vk::Result::eSuccess ||
	    graphics.device.mapMemory(g_marker_memory, 0, VK_WHOLE_SIZE, {}, &mapped) !=
	        vk::Result::eSuccess) {
		Log::WriteToConsoleAndLog("GPU breadcrumbs: marker memory unavailable\n");
		return;
	}
	std::memset(mapped, 0, buffer_info.size);
	g_markers = static_cast<const volatile uint32_t*>(mapped);
	Log::WriteToConsoleAndLog(
	    fmt::format("GPU breadcrumbs: marker buffer ready (memory type {})\n", chosen));
}

uint64_t WidenMarker(uint32_t marker, uint64_t latest) {
	uint64_t value = (latest & ~uint64_t {0xffffffffu}) | marker;
	if (value > latest && value >= (uint64_t {1} << 32u)) {
		value -= uint64_t {1} << 32u;
	}
	return value;
}

void LogBreadcrumbHeartbeat() {
	const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
	                     std::chrono::steady_clock::now().time_since_epoch())
	                     .count();
	auto last = g_heartbeat_ns.load(std::memory_order_relaxed);
	if (now - last < std::chrono::nanoseconds(HEARTBEAT_PERIOD).count() ||
	    !g_heartbeat_ns.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
		return;
	}
	const auto latest    = g_next_sequence.load(std::memory_order_relaxed) - 1;
	const auto started   = WidenMarker(g_markers[MARKER_STARTED], latest);
	const auto completed = WidenMarker(g_markers[MARKER_COMPLETED], latest);
	Log::WriteToConsoleAndLog(
	    fmt::format("GPU breadcrumbs: recorded={} gpu_started={} gpu_completed={}\n", latest,
	                started, completed));
}

void ReportIndirectArgs(const GraphicContext& graphics) {
	if (g_args_slots == nullptr) {
		return;
	}
	std::vector<IndirectArgsSlot> slots;
	for (uint32_t i = 0; i < ARGS_SLOTS; i++) {
		if (g_args_slots[i].sequence != 0) {
			slots.push_back(g_args_slots[i]);
		}
	}
	std::sort(slots.begin(), slots.end(),
	          [](const auto& a, const auto& b) { return a.sequence < b.sequence; });
	const auto& limits = graphics.GetPhysicalDeviceProperties().limits;
	Log::WriteToConsoleAndLog(
	    fmt::format("GPU crash diagnostics: last indirect dispatch args (limit {}x{}x{}):\n",
	                limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1],
	                limits.maxComputeWorkGroupCount[2]));
	const auto first = slots.size() > 8 ? slots.size() - 8 : 0;
	for (auto i = first; i < slots.size(); i++) {
		const auto& slot = slots[i];
		Log::WriteToConsoleAndLog(
		    fmt::format("  seq={} groups={}x{}x{}\n", slot.sequence, slot.x, slot.y, slot.z));
	}
}

const char* OpName(uint32_t op) {
	switch (static_cast<CommandBufferDebugOp>(op)) {
		case CommandBufferDebugOp::DispatchDirect: return "DispatchDirect";
		case CommandBufferDebugOp::DrawIndex: return "DrawIndex";
		case CommandBufferDebugOp::DrawIndexAuto: return "DrawIndexAuto";
		case CommandBufferDebugOp::EopWrite: return "EopWrite";
		case CommandBufferDebugOp::EopInterrupt: return "EopInterrupt";
		case CommandBufferDebugOp::EopWriteBack: return "EopWriteBack";
		case CommandBufferDebugOp::EopFlip: return "EopFlip";
		case CommandBufferDebugOp::EopWriteBackFlip: return "EopWriteBackFlip";
		case CommandBufferDebugOp::EopOnlyFlip: return "EopOnlyFlip";
		case CommandBufferDebugOp::DispatchIndirect: return "DispatchIndirect";
		default: return "Unknown";
	}
}

std::string DescribeSequence(uint64_t sequence) {
	const auto& record = g_ring[sequence % RING_SIZE];
	if (record.sequence.load(std::memory_order_acquire) != sequence) {
		return fmt::format("seq={} (record overwritten)", sequence);
	}
	const auto& info = record.info;
	return fmt::format("seq={} op={} submit={} args={},{},{},{},0x{:016x} ps=0x{:016x} "
	                   "es=0x{:016x} gs=0x{:016x} shader_hash={:016x}",
	                   sequence, OpName(info.op), info.submit_id, info.args[0], info.args[1],
	                   info.args[2], info.args[3], info.arg4, info.ps_addr, info.es_addr,
	                   info.gs_addr, info.shader_hash);
}

void ReportBreadcrumbs() {
	if (g_markers == nullptr) {
		Log::WriteToConsoleAndLog("GPU breadcrumbs: no marker buffer\n");
		return;
	}
	const auto latest    = g_next_sequence.load(std::memory_order_relaxed) - 1;
	const auto started   = WidenMarker(g_markers[MARKER_STARTED], latest);
	const auto completed = WidenMarker(g_markers[MARKER_COMPLETED], latest);
	// Everything before `completed` finished and nothing after `started` began, so the
	// hung or faulting command is in [completed, started].
	Log::WriteToConsoleAndLog(fmt::format(
	    "GPU breadcrumbs: recorded={} gpu_started={} gpu_completed={} suspects={}\n", latest,
	    started, completed, started >= completed ? started - completed + 1 : 0));
	if (started < completed || started == 0) {
		return;
	}
	constexpr uint64_t EDGE  = 12;
	const uint64_t     count = started - completed + 1;
	for (uint64_t sequence = completed; sequence <= started; sequence++) {
		const uint64_t index = sequence - completed;
		if (count > EDGE * 2 && index == EDGE) {
			Log::WriteToConsoleAndLog(
			    fmt::format("  ... {} suspect(s) omitted ...\n", count - EDGE * 2));
			sequence = started - EDGE;
			continue;
		}
		Log::WriteToConsoleAndLog(
		    fmt::format("GPU breadcrumb suspect: {}\n", DescribeSequence(sequence)));
	}
}

void ReportCheckpoints(const GraphicContext& graphics) {
	const auto checkpoints = graphics.queue.getCheckpointDataNV();
	Log::WriteToConsoleAndLog(
	    fmt::format("GPU crash diagnostics: {} checkpoint(s), last recorded seq={}\n",
	                checkpoints.size(), g_next_sequence.load(std::memory_order_relaxed) - 1));
	for (const auto& checkpoint: checkpoints) {
		const auto sequence = reinterpret_cast<uint64_t>(checkpoint.pCheckpointMarker);
		Log::WriteToConsoleAndLog(fmt::format("  stage={} {}\n", vk::to_string(checkpoint.stage),
		                                      DescribeSequence(sequence)));
	}
}

void ReportDeviceFault(const GraphicContext& graphics) {
	vk::DeviceFaultCountsEXT counts {};
	if (graphics.device.getFaultInfoEXT(&counts, nullptr) != vk::Result::eSuccess) {
		Log::WriteToConsoleAndLog("GPU crash diagnostics: vkGetDeviceFaultInfoEXT failed\n");
		return;
	}
	std::vector<vk::DeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
	std::vector<vk::DeviceFaultVendorInfoEXT>  vendors(counts.vendorInfoCount);
	counts.vendorBinarySize = 0;
	vk::DeviceFaultInfoEXT info {};
	info.pAddressInfos = addresses.data();
	info.pVendorInfos  = vendors.data();
	const auto result  = graphics.device.getFaultInfoEXT(&counts, &info);
	if (result != vk::Result::eSuccess && result != vk::Result::eIncomplete) {
		Log::WriteToConsoleAndLog("GPU crash diagnostics: vkGetDeviceFaultInfoEXT failed\n");
		return;
	}
	Log::WriteToConsoleAndLog(fmt::format("GPU device fault: \"{}\" addresses={} vendor_infos={}\n",
	                                      info.description.data(), counts.addressInfoCount,
	                                      counts.vendorInfoCount));
	for (uint32_t i = 0; i < counts.addressInfoCount; i++) {
		const auto& address = addresses[i];
		Log::WriteToConsoleAndLog(fmt::format(
		    "  address type={} reported=0x{:016x} precision=0x{:x}\n",
		    vk::to_string(address.addressType), address.reportedAddress, address.addressPrecision));
	}
	for (uint32_t i = 0; i < counts.vendorInfoCount; i++) {
		const auto& vendor = vendors[i];
		Log::WriteToConsoleAndLog(fmt::format("  vendor \"{}\" code=0x{:016x} data=0x{:016x}\n",
		                                      vendor.description.data(), vendor.vendorFaultCode,
		                                      vendor.vendorFaultData));
	}
}

} // namespace

void RecordCheckpoint(const GraphicContext& graphics, vk::CommandBuffer buffer,
                      const CheckpointInfo& info) {
	if ((!graphics.checkpoints_enabled && !graphics.buffer_markers_enabled) || buffer == nullptr) {
		return;
	}
	const auto sequence = g_next_sequence.fetch_add(1, std::memory_order_relaxed);
	auto&      record   = g_ring[sequence % RING_SIZE];
	record.sequence.store(0, std::memory_order_relaxed);
	record.info = info;
	record.sequence.store(sequence, std::memory_order_release);
	t_last_sequence = sequence;
	if (graphics.checkpoints_enabled) {
		buffer.setCheckpointNV(reinterpret_cast<const void*>(sequence));
	}
	if (graphics.buffer_markers_enabled) {
		std::call_once(g_marker_once, [&] { CreateMarkerBuffer(graphics); });
		if (g_marker_buffer == nullptr || g_markers == nullptr) {
			return;
		}
		const auto marker = static_cast<uint32_t>(sequence);
		buffer.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eTopOfPipe, g_marker_buffer,
		                            sizeof(uint32_t) * MARKER_STARTED, marker);
		buffer.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, g_marker_buffer,
		                            sizeof(uint32_t) * MARKER_COMPLETED, marker);
		if (sequence % HEARTBEAT_STRIDE == 0) {
			LogBreadcrumbHeartbeat();
		}
	}
}

void AnnotateLastCheckpoint(uint64_t shader_hash) {
	if (t_last_sequence == 0) {
		return;
	}
	auto& record = g_ring[t_last_sequence % RING_SIZE];
	if (record.sequence.load(std::memory_order_acquire) == t_last_sequence) {
		record.info.shader_hash = shader_hash;
	}
}

void RecordIndirectArgs(const GraphicContext& graphics, vk::CommandBuffer buffer,
                        vk::Buffer args_buffer, uint64_t args_offset) {
	if (!graphics.checkpoints_enabled || buffer == nullptr || args_buffer == nullptr) {
		return;
	}
	std::call_once(g_args_once, [&] { CreateArgsRing(graphics); });
	if (g_args_slots == nullptr) {
		return;
	}
	const uint32_t    slot   = g_args_next.fetch_add(1, std::memory_order_relaxed) % ARGS_SLOTS;
	const uint64_t    offset = uint64_t {slot} * sizeof(IndirectArgsSlot);
	vk::MemoryBarrier barrier {};
	barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
	buffer.pipelineBarrier(
	    vk::PipelineStageFlagBits::eAllGraphics | vk::PipelineStageFlagBits::eComputeShader |
	        vk::PipelineStageFlagBits::eTransfer,
	    vk::PipelineStageFlagBits::eTransfer, {}, 1, &barrier, 0, nullptr, 0, nullptr);
	const vk::BufferCopy copy {args_offset, offset, sizeof(uint32_t) * 3};
	buffer.copyBuffer(args_buffer, g_args_buffer, 1, &copy);
	const auto sequence = static_cast<uint32_t>(t_last_sequence);
	buffer.updateBuffer(g_args_buffer, offset + sizeof(uint32_t) * 3, sizeof(sequence), &sequence);
	vk::MemoryBarrier published {};
	published.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	published.dstAccessMask = vk::AccessFlagBits::eHostRead;
	buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                       vk::PipelineStageFlagBits::eAllCommands |
	                           vk::PipelineStageFlagBits::eHost,
	                       {}, 1, &published, 0, nullptr, 0, nullptr);
}

void RecordLoopCountBuffer(const GraphicContext& graphics, vk::CommandBuffer command,
                           const vk::DescriptorBufferInfo& source, uint64_t guest_address) {
	if (!graphics.checkpoints_enabled || source.buffer == nullptr ||
	    source.range == VK_WHOLE_SIZE || source.range < sizeof(uint32_t))
		return;
	if (!g_snapshot_attempted) {
		g_snapshot_attempted = true;
		vk::BufferCreateInfo info {};
		info.size  = SNAPSHOT_STRIDE * SNAPSHOT_SLOTS;
		info.usage = vk::BufferUsageFlagBits::eTransferDst;
		if (graphics.device.createBuffer(&info, nullptr, &g_snapshot_buffer) !=
		    vk::Result::eSuccess)
			return;
		const auto requirements = graphics.device.getBufferMemoryRequirements(g_snapshot_buffer);
		const auto wanted =
		    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
		const auto& memory = graphics.GetPhysicalDeviceMemoryProperties();
		for (uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
			if ((requirements.memoryTypeBits & (1u << type)) == 0 ||
			    (memory.memoryTypes[type].propertyFlags & wanted) != wanted)
				continue;
			vk::MemoryAllocateInfo allocate {};
			allocate.allocationSize  = requirements.size;
			allocate.memoryTypeIndex = type;
			void* mapped             = nullptr;
			if (graphics.device.allocateMemory(&allocate, nullptr, &g_snapshot_memory) !=
			        vk::Result::eSuccess ||
			    graphics.device.bindBufferMemory(g_snapshot_buffer, g_snapshot_memory, 0) !=
			        vk::Result::eSuccess ||
			    graphics.device.mapMemory(g_snapshot_memory, 0, VK_WHOLE_SIZE, {}, &mapped) !=
			        vk::Result::eSuccess)
				return;
			std::memset(mapped, 0, info.size);
			g_snapshot_words = static_cast<const uint32_t*>(mapped);
			break;
		}
	}
	if (g_snapshot_words == nullptr) return;
	const uint64_t         slot_offset = (g_snapshot_next++ % SNAPSHOT_SLOTS) * SNAPSHOT_STRIDE;
	const SnapshotMetadata metadata {
	    t_last_sequence, std::min<uint64_t>(source.range, SNAPSHOT_LIMIT) & ~uint64_t {3},
	    guest_address, source.offset};
	Log::WriteToConsoleAndLog(fmt::format("GPU loop-count snapshot queued: seq={} binding=2 "
	                                      "guest=0x{:012x} offset={} range={} captured={}\n",
	                                      metadata.sequence, guest_address, source.offset,
	                                      source.range, metadata.bytes));
	vk::MemoryBarrier before {};
	before.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
	before.dstAccessMask = vk::AccessFlagBits::eTransferRead;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
	                        vk::PipelineStageFlagBits::eTransfer, {}, 1, &before, 0, nullptr, 0,
	                        nullptr);
	const SnapshotMetadata empty {};
	command.updateBuffer(g_snapshot_buffer, slot_offset + SNAPSHOT_LIMIT, sizeof(empty), &empty);
	const vk::BufferCopy copy {source.offset, slot_offset, metadata.bytes};
	command.copyBuffer(source.buffer, g_snapshot_buffer, 1, &copy);
	vk::MemoryBarrier copied {};
	copied.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	copied.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                        vk::PipelineStageFlagBits::eTransfer, {}, 1, &copied, 0, nullptr, 0,
	                        nullptr);
	command.updateBuffer(g_snapshot_buffer, slot_offset + SNAPSHOT_LIMIT, sizeof(metadata),
	                     &metadata);
	vk::MemoryBarrier after {};
	after.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	after.dstAccessMask = vk::AccessFlagBits::eHostRead | vk::AccessFlagBits::eShaderRead;
	command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
	                        vk::PipelineStageFlagBits::eHost |
	                            vk::PipelineStageFlagBits::eComputeShader,
	                        {}, 1, &after, 0, nullptr, 0, nullptr);
}

void ReportDeviceLost(const GraphicContext& graphics) {
	if ((!graphics.checkpoints_enabled && !graphics.device_fault_enabled &&
	     !graphics.buffer_markers_enabled) ||
	    g_reported.test_and_set(std::memory_order_acq_rel)) {
		return;
	}
	if (graphics.buffer_markers_enabled) {
		ReportBreadcrumbs();
	}
	if (graphics.checkpoints_enabled) {
		ReportCheckpoints(graphics);
		ReportIndirectArgs(graphics);
		ReportLoopCountBuffer();
	}
	if (graphics.device_fault_enabled) {
		ReportDeviceFault(graphics);
	}
}

} // namespace Libs::Graphics::GpuCrashDiagnostics
