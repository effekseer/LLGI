#include "TestHelper.h"
#include "TextureUploadReadback.h"

#include <cstring>

namespace
{
void TestTextureUpload(LLGI::DeviceType device, bool isArray)
{
	LLGI::PlatformParameter platformParameter;
	platformParameter.Device = device;
	platformParameter.WaitVSync = false;
	auto window = std::unique_ptr<LLGI::Window>(LLGI::CreateWindow("TextureUpload", {64, 64}));
	auto platform = LLGI::CreateSharedPtr(LLGI::CreatePlatform(platformParameter, window.get()));
	VERIFY(platform != nullptr);
	auto graphics = LLGI::CreateSharedPtr(platform->CreateGraphics());
	VERIFY(graphics != nullptr);

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
		graphics->WaitFinish();

		size_t offset = 0;
		for (int32_t mip = 0; mip < parameter.MipLevelCount; mip++)
		{
			const auto mipSize = LLGI::GetTextureMipSize(size, mip, isArray);
			const size_t planeBytes = static_cast<size_t>(mipSize.X) * mipSize.Y * 4;
			// Read one plane at a time so readback cannot repeat the upload stride bug.
			for (int32_t z = 0; z < mipSize.Z; z++)
			{
				const auto actual = ReadTextureUploadPlane(graphics.get(), device, texture.get(), mip, z, {mipSize.X, mipSize.Y}, isArray);
				VERIFY(actual.size() == planeBytes);
				if (memcmp(actual.data(), expected.data() + offset, planeBytes) != 0)
				{
					std::cerr << "Texture upload mismatch: size=" << size.X << "," << size.Y << "," << size.Z
							  << " array=" << isArray << " mip=" << mip << " layer=" << z << std::endl;
					VERIFY(false);
				}
				offset += planeBytes;
			}
		}
		VERIFY(offset == expected.size());
	}
	graphics->WaitFinish();
}
} // namespace

TestRegister TextureUpload_3D("TextureUpload.3D", [](LLGI::DeviceType device) { TestTextureUpload(device, false); });
TestRegister TextureUpload_Array("TextureUpload.Array", [](LLGI::DeviceType device) { TestTextureUpload(device, true); });
