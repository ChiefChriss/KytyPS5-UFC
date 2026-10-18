#include "graphics/host_gpu/vulkanCommon.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/masterSemaphore.h"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <cstring>

namespace Libs::Graphics {

namespace {

// KYTY_UFC_IMAGE_POOL=1 (opt-in, default OFF): recycle freed VkImage+VmaAllocation backings with
// identical create parameters in CreateImage/DeleteImage. Bounded by a byte cap and a count cap so
// it cannot grow VRAM use without limit.
bool ImagePoolEnabled() {
	static const bool enabled = [] {
		const char* value = std::getenv("KYTY_UFC_IMAGE_POOL");
		return value != nullptr && std::strcmp(value, "1") == 0;
	}();
	return enabled;
}

uint64_t ImagePoolByteCap() {
	static const uint64_t cap = [] {
		const char* value = std::getenv("KYTY_UFC_IMAGE_POOL_BYTES");
		if (value != nullptr && *value != '\0') {
			return static_cast<uint64_t>(std::strtoull(value, nullptr, 10));
		}
		return uint64_t {512} * 1024 * 1024;
	}();
	return cap;
}

size_t ImagePoolCountCap() {
	static const size_t cap = [] {
		const char* value = std::getenv("KYTY_UFC_IMAGE_POOL_COUNT");
		if (value != nullptr && *value != '\0') {
			return static_cast<size_t>(std::strtoull(value, nullptr, 10));
		}
		return size_t {2048};
	}();
	return cap;
}

bool PooledImageMatches(const PooledNativeImage& pooled, const vk::ImageCreateInfo& info) {
	return pooled.format == info.format && pooled.image_type == info.imageType &&
	       pooled.extent == info.extent && pooled.layers == info.arrayLayers &&
	       pooled.mip_levels == info.mipLevels &&
	       pooled.samples == static_cast<uint32_t>(info.samples) && pooled.usage == info.usage &&
	       pooled.flags == info.flags && pooled.tiling == info.tiling &&
	       pooled.sharing == info.sharingMode && pooled.initial_layout == info.initialLayout;
}

} // namespace

bool GraphicContext::TryReusePooledImage(const vk::ImageCreateInfo& image_info, VulkanImage& image) {
	for (size_t i = 0; i < m_image_pool.size(); i++) {
		PooledNativeImage& pooled = m_image_pool[i];
		if (!PooledImageMatches(pooled, image_info)) {
			continue;
		}
		const uint64_t bytes = pooled.bytes;
		image.image          = pooled.image;
		image.allocation     = pooled.allocation;
		image.format         = image_info.format;
		image.image_type     = image_info.imageType;
		image.extent         = image_info.extent;
		image.layers         = image_info.arrayLayers;
		image.mip_levels     = image_info.mipLevels;
		image.samples        = static_cast<uint32_t>(image_info.samples);
		image.usage          = image_info.usage;
		image.flags          = image_info.flags;
		image.state          = {.layout = image_info.initialLayout};
		image.subresource_states.clear();
		m_image_pool.erase(m_image_pool.begin() + static_cast<ptrdiff_t>(i));
		m_image_pool_bytes -= bytes;
		WaitStats::NoteImagePool(0, bytes);
		return true;
	}
	return false;
}

void GraphicContext::EvictOnePooledImage() {
	if (m_image_pool.empty()) {
		return;
	}
	PooledNativeImage pooled = m_image_pool.back();
	m_image_pool.pop_back();
	m_image_pool_bytes -= std::min(m_image_pool_bytes, pooled.bytes);
	vmaDestroyImage(allocator, pooled.image, pooled.allocation);
	WaitStats::NoteImagePool(2);
}

bool GraphicContext::CreateAllocator() {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(instance == nullptr || physical_device == nullptr || device == nullptr ||
	        allocator != nullptr);

	VmaVulkanFunctions functions {};
	functions.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
	functions.vkGetDeviceProcAddr   = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo info {};
	info.instance         = instance;
	info.physicalDevice   = physical_device;
	info.device           = device;
	info.pVulkanFunctions = &functions;
	info.vulkanApiVersion = VULKAN_TARGET_API_VERSION;
	info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	if (memory_budget_ext_enabled) {
		info.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	}

	const auto result = static_cast<vk::Result>(vmaCreateAllocator(&info, &allocator));
	if (result != vk::Result::eSuccess) {
		LOGF("vmaCreateAllocator failed: %s\n", vk::to_string(result).c_str());
		return false;
	}
	return true;
}

void GraphicContext::DestroyAllocator() {
	if (allocator == nullptr) {
		return;
	}
	while (!m_image_pool.empty()) {
		EvictOnePooledImage();
	}
	vmaDestroyAllocator(allocator);
	allocator = nullptr;
}

void GraphicContext::LogMemoryBudget() const {
	if (allocator == nullptr || physical_device == nullptr) {
		return;
	}

	const auto& properties = GetPhysicalDeviceMemoryProperties();
	VmaBudget   budgets[VK_MAX_MEMORY_HEAPS] {};
	vmaGetHeapBudgets(allocator, budgets);
	for (uint32_t i = 0; i < properties.memoryHeapCount; i++) {
		LOGF("VMA heap %u: usage=%" PRIu64 ", budget=%" PRIu64 ", allocation=%" PRIu64
		     ", blocks=%" PRIu64 "\n",
		     i, static_cast<uint64_t>(budgets[i].usage), static_cast<uint64_t>(budgets[i].budget),
		     static_cast<uint64_t>(budgets[i].statistics.allocationBytes),
		     static_cast<uint64_t>(budgets[i].statistics.blockBytes));
	}
}

uint64_t GraphicContext::GetDeviceMemoryUsage() const {
	if (!CanReportMemoryUsage() || allocator == nullptr) {
		return 0;
	}
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS] {};
	vmaGetHeapBudgets(allocator, budgets);
	const bool discrete =
	    physical_device_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	uint64_t usage = 0;
	for (uint32_t heap = 0; heap < physical_device_memory_properties.memoryHeapCount; heap++) {
		const bool device_local =
		    static_cast<bool>(physical_device_memory_properties.memoryHeaps[heap].flags &
		                      vk::MemoryHeapFlagBits::eDeviceLocal);
		if (!discrete || device_local) {
			usage += budgets[heap].usage;
		}
	}
	return usage;
}

uint64_t GraphicContext::GetTotalMemoryBudget() const {
	if (allocator == nullptr) {
		return 0;
	}
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS] {};
	vmaGetHeapBudgets(allocator, budgets);
	const bool discrete =
	    physical_device_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
	uint64_t budget = 0;
	uint64_t local  = 0;
	uint64_t usage  = 0;
	for (uint32_t heap = 0; heap < physical_device_memory_properties.memoryHeapCount; heap++) {
		const auto& properties = physical_device_memory_properties.memoryHeaps[heap];
		const bool  device_local =
		    static_cast<bool>(properties.flags & vk::MemoryHeapFlagBits::eDeviceLocal);
		if (device_local) {
			local += properties.size;
		}
		if (!discrete || device_local) {
			budget += CanReportMemoryUsage() ? budgets[heap].budget : properties.size;
			usage += CanReportMemoryUsage() ? budgets[heap].usage : 0;
		}
	}
	if (discrete) {
		return budget - std::min<uint64_t>(budget / 8, 1024ull * 1024 * 1024);
	}
	constexpr uint64_t system_reserve = 8ull * 1024 * 1024 * 1024;
	const auto         available      = budget > usage ? budget - usage : uint64_t {0};
	return std::max(local, available > system_reserve ? available - system_reserve : uint64_t {0});
}

bool GraphicContext::CreateImage(const vk::ImageCreateInfo& image_info, VulkanImage& image) {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(allocator == nullptr || image.image != nullptr || image.allocation != nullptr);

	if (ImagePoolEnabled()) {
		if (TryReusePooledImage(image_info, image)) {
			return true;
		}
		WaitStats::NoteImagePool(1);
	}

	VmaAllocationCreateInfo alloc_info {};
	alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	vk::Image::CType native_image = VK_NULL_HANDLE;
	const auto        result       = static_cast<vk::Result>(
	    vmaCreateImage(allocator, static_cast<const vk::ImageCreateInfo::NativeType*>(image_info),
	                   &alloc_info, &native_image, &image.allocation, nullptr));
	image.image = native_image;
	if (result != vk::Result::eSuccess) {
		LogMemoryBudget();
		return false;
	}

	image.format     = image_info.format;
	image.image_type = image_info.imageType;
	image.extent     = image_info.extent;
	image.layers     = image_info.arrayLayers;
	image.mip_levels = image_info.mipLevels;
	image.samples    = static_cast<uint32_t>(image_info.samples);
	image.usage      = image_info.usage;
	image.flags      = image_info.flags;
	image.state      = {.layout = image_info.initialLayout};
	image.subresource_states.clear();

	return true;
}

void GraphicContext::DeleteImage(VulkanImage& image) {
	KYTY_PROFILER_FUNCTION();
	EXIT_IF(allocator == nullptr || image.image == nullptr || image.allocation == nullptr);

	if (ImagePoolEnabled() && image.image_type != vk::ImageType::e1D) {
		PooledNativeImage pooled {};
		pooled.image          = image.image;
		pooled.allocation     = image.allocation;
		pooled.format         = image.format;
		pooled.image_type     = image.image_type;
		pooled.extent         = image.extent;
		pooled.layers         = image.layers;
		pooled.mip_levels     = image.mip_levels;
		pooled.samples        = image.samples;
		pooled.usage          = image.usage;
		pooled.flags          = image.flags;
		pooled.sharing        = vk::SharingMode::eExclusive;
		pooled.initial_layout = vk::ImageLayout::eUndefined;
		VmaAllocationInfo alloc_info {};
		vmaGetAllocationInfo(allocator, image.allocation, &alloc_info);
		pooled.bytes = alloc_info.size;

		const uint64_t byte_cap = ImagePoolByteCap();
		const size_t   count_cap = ImagePoolCountCap();
		while (!m_image_pool.empty() &&
		       (m_image_pool.size() >= count_cap || m_image_pool_bytes + pooled.bytes > byte_cap)) {
			EvictOnePooledImage();
		}
		if (m_image_pool.size() < count_cap && m_image_pool_bytes + pooled.bytes <= byte_cap) {
			m_image_pool_bytes += pooled.bytes;
			m_image_pool.push_back(pooled);
			image.image      = nullptr;
			image.allocation = nullptr;
			return;
		}
	}

	vmaDestroyImage(allocator, image.image, image.allocation);
	image.image      = nullptr;
	image.allocation = nullptr;
}

} // namespace Libs::Graphics
