#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include "graphics/host_gpu/vulkanCommon.h"

namespace Libs::Graphics {

// Cross-type copies map 2D array layers to 3D depth slices. A 3D extent
// must never be used as the array-layer count of the 2D image unchecked.
inline std::pair<uint32_t, uint32_t> ImageCopyLayerCounts(
    vk::ImageType source_type, uint32_t source_layers,
    vk::ImageType destination_type, uint32_t destination_layers, uint32_t depth) {
	if (source_type == vk::ImageType::e3D) source_layers = 1;
	if (destination_type == vk::ImageType::e3D) destination_layers = 1;
	if (source_type == destination_type) {
		source_layers = destination_layers = std::min(source_layers, destination_layers);
	} else if (source_type == vk::ImageType::e2D && destination_type == vk::ImageType::e3D) {
		source_layers = std::min(source_layers, depth);
	} else if (source_type == vk::ImageType::e3D && destination_type == vk::ImageType::e2D) {
		destination_layers = std::min(destination_layers, depth);
	}
	return {source_layers, destination_layers};
}

} // namespace Libs::Graphics
