#include "TextureUploadReadback.h"
#include "TestHelper.h"
#include <cstring>

#ifdef ENABLE_WEBGPU
#include <WebGPU/LLGI.BufferWebGPU.h>
#include <WebGPU/LLGI.GraphicsWebGPU.h>
#include <WebGPU/LLGI.TextureWebGPU.h>

std::vector<uint8_t> ReadTextureUploadPlaneWebGPU(
	LLGI::Graphics* graphics, LLGI::Texture* texture, int32_t mip, int32_t layer, const LLGI::Vec2I& size)
{
	const uint32_t rowPitch = (static_cast<uint32_t>(size.X) * 4 + 255) / 256 * 256;
	auto readback = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::CopyDst | LLGI::BufferUsageType::MapRead,
															 rowPitch * size.Y));
	VERIFY(readback != nullptr);
	auto nativeGraphics = static_cast<LLGI::GraphicsWebGPU*>(graphics);
	auto encoder = nativeGraphics->GetDevice().CreateCommandEncoder();
	wgpu::TexelCopyTextureInfo src{};
	src.texture = static_cast<LLGI::TextureWebGPU*>(texture)->GetTexture();
	src.mipLevel = mip;
	src.origin.z = layer;
	src.aspect = wgpu::TextureAspect::All;
	wgpu::TexelCopyBufferInfo dst{};
	dst.buffer = static_cast<LLGI::BufferWebGPU*>(readback.get())->GetBuffer();
	dst.layout.bytesPerRow = rowPitch;
	dst.layout.rowsPerImage = size.Y;
	wgpu::Extent3D extent{};
	extent.width = size.X;
	extent.height = size.Y;
	extent.depthOrArrayLayers = 1;
	encoder.CopyTextureToBuffer(&src, &dst, &extent);
	auto commands = encoder.Finish();
	nativeGraphics->GetQueue().Submit(1, &commands);
	// Mapping waits for this buffer's copy, including in the browser build.
	const auto mapped = static_cast<const uint8_t*>(readback->Lock());
	VERIFY(mapped != nullptr);
	const size_t rowBytes = static_cast<size_t>(size.X) * 4;
	std::vector<uint8_t> data(rowBytes * size.Y);
	for (int32_t y = 0; y < size.Y; y++)
	{
		memcpy(data.data() + y * rowBytes, mapped + y * rowPitch, rowBytes);
	}
	readback->Unlock();
	return data;
}
#endif
