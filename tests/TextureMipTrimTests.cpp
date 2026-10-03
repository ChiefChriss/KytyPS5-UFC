#include "graphics/guest_gpu/tile.h"
#include "graphics/host_gpu/renderer/image/imageView.h"
#include "graphics/host_gpu/renderer/image/copyGeometry.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "TextureMipTrimTests: %s\n", message);
		std::abort();
	}
}

int main() {
	using namespace Libs::Graphics;
	// UFC 5 descriptor c4500000 / 007fc07f / 007000a0: 512x512,
	// format 69, tile 9, MAX_MIP=10 (11 levels), a view of mip 0.
	const TileSurfaceDescription original {
	    static_cast<Prospero::BufferFormat>(69), static_cast<Prospero::TileMode>(9),
	    TileSurfaceDimension::Dim2D, 512, 512, 1, 11, 1};
	Check(TileCanTrimMipLevels(original, 10), "UFC excess tail mip cannot be trimmed safely");
	Check(!TileCanTrimMipLevels(original, 1), "changed mip-tail layout was accepted");
	Check(!TileCanTrimMipLevels(original, 0), "zero mip allocation was accepted");
	Check(!TileCanTrimMipLevels(original, 12), "mip expansion was accepted");
	uint64_t last_offset = 0, extra_offset = 0;
	Check(TileGetSingleTexelMipOffset(original, 9, last_offset), "last mip offset missing");
	Check(TileGetSingleTexelMipOffset(original, 10, extra_offset), "excess mip offset missing");
	Check(last_offset != extra_offset, "extra mip aliases the last legal mip");
	Check(!TileGetSingleTexelMipOffset(original, 0, extra_offset), "non-single-texel mip accepted");
	Check(!TileGetSingleTexelMipOffset(original, 11, extra_offset), "out-of-range mip accepted");
	Check(ImageCopyLayerCounts(vk::ImageType::e2D, 1, vk::ImageType::e3D, 1, 16) ==
	          std::pair<uint32_t, uint32_t>{1, 1}, "single-layer source expanded to 16 layers");
	Check(ImageCopyLayerCounts(vk::ImageType::e3D, 1, vk::ImageType::e2D, 1, 16) ==
	          std::pair<uint32_t, uint32_t>{1, 1}, "single-layer destination expanded to 16 layers");
	Check(ImageCopyLayerCounts(vk::ImageType::e2D, 32, vk::ImageType::e3D, 1, 16) ==
	          std::pair<uint32_t, uint32_t>{16, 1}, "valid array-to-volume copy changed");
	Check(ImageCopyLayerCounts(vk::ImageType::e3D, 1, vk::ImageType::e2D, 32, 16) ==
	          std::pair<uint32_t, uint32_t>{1, 16}, "valid volume-to-array copy changed");
	const auto backing_usage = vk::ImageUsageFlagBits::eSampled |
	                           vk::ImageUsageFlagBits::eStorage |
	                           vk::ImageUsageFlagBits::eColorAttachment |
	                           vk::ImageUsageFlagBits::eTransferSrc;
	const auto rgb9e5_features = vk::FormatFeatureFlags {vk::FormatFeatureFlagBits::eSampledImage};
	const auto rgba8_features  = vk::FormatFeatureFlagBits::eSampledImage |
	                            vk::FormatFeatureFlagBits::eColorAttachment |
	                            vk::FormatFeatureFlagBits::eStorageImage;
	Check(ImageViewOps::ResolveViewUsage(backing_usage, false, rgb9e5_features) ==
	          (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc),
	      "sampled RGB9E5 reinterpretation inherited attachment/storage usage");
	Check(ImageViewOps::ResolveViewUsage(backing_usage, false, rgba8_features) ==
	          (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
	           vk::ImageUsageFlagBits::eTransferSrc),
	      "non-storage view kept storage usage or lost attachment usage");
	Check(ImageViewOps::ResolveViewUsage(backing_usage, true, rgba8_features) == backing_usage,
	      "storage view lost backing usage");
	std::puts("TextureMipTrimTests: all cases passed");
}
