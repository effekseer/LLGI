#include "TextureUploadReadback.h"
#include <Metal/LLGI.TextureMetal.h>

#import <Metal/Metal.h>

std::vector<uint8_t> ReadTextureUploadPlaneMetal(
	LLGI::Texture* texture, int32_t mip, int32_t layer, const LLGI::Vec2I& size, bool isArray)
{
	@autoreleasepool
	{
		const size_t rowBytes = static_cast<size_t>(size.X) * 4;
		std::vector<uint8_t> data(rowBytes * size.Y);
		auto nativeTexture = static_cast<LLGI::TextureMetal*>(texture)->GetTexture();
		const auto region = MTLRegionMake3D(0, 0, isArray ? 0 : layer, size.X, size.Y, 1);
		[nativeTexture getBytes:data.data()
				   bytesPerRow:rowBytes
				 bytesPerImage:data.size()
					fromRegion:region
				   mipmapLevel:mip
						 slice:isArray ? layer : 0];
		return data;
	}
}
