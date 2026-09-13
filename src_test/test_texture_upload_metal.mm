#include "TestHelper.h"
#include <Metal/LLGI.GraphicsMetal.h>
#include <Metal/LLGI.TextureMetal.h>

#import <Metal/Metal.h>

#include <cstring>

namespace
{
void TestTextureUpload(LLGI::DeviceType device, bool isArray)
{
	if (device != LLGI::DeviceType::Metal)
	{
		return;
	}

	@autoreleasepool
	{
		auto graphics = LLGI::CreateSharedPtr(new LLGI::GraphicsMetal());
		VERIFY(graphics->Initialize({}));

		for (const auto size : {LLGI::Vec3I{2, 2, 2}, LLGI::Vec3I{8, 4, 4}, LLGI::Vec3I{7, 5, 3}})
		{
			LLGI::TextureParameter parameter;
			parameter.Dimension = isArray ? 2 : 3;
			parameter.Usage = isArray ? LLGI::TextureUsageType::Array : LLGI::TextureUsageType::NoneFlag;
			parameter.Size = size;
			parameter.MipLevelCount = 2;
			auto texture = LLGI::CreateSharedPtr(graphics->CreateTexture(parameter));
			VERIFY(texture != nullptr);

			// Distinct bytes across rows, slices, and mips expose incorrect upload strides.
			std::vector<uint8_t> expected(LLGI::GetTextureMemorySize(parameter.Format, size, parameter.MipLevelCount, isArray));
			for (size_t i = 0; i < expected.size(); i++)
			{
				expected[i] = static_cast<uint8_t>((i * 37 + i / 11) % 251);
			}
			auto destination = texture->Lock();
			VERIFY(destination != nullptr);
			memcpy(destination, expected.data(), expected.size());
			texture->Unlock();

			auto nativeTexture = static_cast<LLGI::TextureMetal*>(texture.get())->GetTexture();
			std::vector<uint8_t> actual(expected.size());
			size_t offset = 0;
			for (int32_t mip = 0; mip < parameter.MipLevelCount; mip++)
			{
				const auto mipSize = LLGI::GetTextureMipSize(size, mip, isArray);
				const size_t rowBytes = static_cast<size_t>(mipSize.X) * 4;
				const size_t planeBytes = rowBytes * mipSize.Y;
				// Read one Z plane at a time so readback cannot repeat the upload stride bug.
				for (int32_t z = 0; z < mipSize.Z; z++)
				{
					const auto region = MTLRegionMake3D(0, 0, isArray ? 0 : z, mipSize.X, mipSize.Y, 1);
					[nativeTexture getBytes:actual.data() + offset
							   bytesPerRow:rowBytes
							 bytesPerImage:planeBytes
								fromRegion:region
							   mipmapLevel:mip
									 slice:isArray ? z : 0];
					offset += planeBytes;
				}
			}
			VERIFY(actual == expected);
		}
	}
}
} // namespace

TestRegister TextureUpload_Metal3D("TextureUpload.Metal3D", [](LLGI::DeviceType device) { TestTextureUpload(device, false); });
TestRegister TextureUpload_MetalArray("TextureUpload.MetalArray", [](LLGI::DeviceType device) { TestTextureUpload(device, true); });
