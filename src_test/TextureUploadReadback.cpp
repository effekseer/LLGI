#include "TextureUploadReadback.h"
#include "TestHelper.h"
#include <cstring>

#ifdef _WIN32
#include <DX12/LLGI.BufferDX12.h>
#include <DX12/LLGI.CommandListDX12.h>
#include <DX12/LLGI.TextureDX12.h>
#endif
#ifdef ENABLE_VULKAN
#include <Vulkan/LLGI.BufferVulkan.h>
#include <Vulkan/LLGI.CommandListVulkan.h>
#include <Vulkan/LLGI.TextureVulkan.h>
#endif

namespace
{
#if defined(_WIN32) || defined(ENABLE_VULKAN)
std::vector<uint8_t> ReadRows(LLGI::Buffer* buffer, const LLGI::Vec2I& size, size_t rowPitch)
{
	const auto mapped = static_cast<const uint8_t*>(buffer->Lock());
	VERIFY(mapped != nullptr);
	const size_t rowBytes = static_cast<size_t>(size.X) * 4;
	std::vector<uint8_t> data(rowBytes * size.Y);
	for (int32_t y = 0; y < size.Y; y++)
	{
		memcpy(data.data() + y * rowBytes, mapped + y * rowPitch, rowBytes);
	}
	buffer->Unlock();
	return data;
}
#endif

#ifdef _WIN32
std::vector<uint8_t> ReadDX12(LLGI::Graphics* graphics, LLGI::Texture* texture, int32_t mip, int32_t layer,
							const LLGI::Vec2I& size, bool isArray)
{
	const UINT rowPitch = (static_cast<UINT>(size.X) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
						  D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	auto readback = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::CopyDst | LLGI::BufferUsageType::MapRead,
															 rowPitch * size.Y));
	VERIFY(readback != nullptr);
	auto pool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024, 1));
	VERIFY(pool != nullptr);
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(pool.get()));
	VERIFY(commands != nullptr);
	commands->Begin();
	auto nativeCommands = static_cast<LLGI::CommandListDX12*>(commands.get())->GetCommandList();
	auto nativeTexture = static_cast<LLGI::TextureDX12*>(texture);
	const auto oldState = nativeTexture->GetState();
	nativeTexture->ResourceBarrier(nativeCommands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	D3D12_TEXTURE_COPY_LOCATION src{};
	src.pResource = nativeTexture->Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = mip + (isArray ? layer * nativeTexture->GetParameter().MipLevelCount : 0);
	D3D12_TEXTURE_COPY_LOCATION dst{};
	dst.pResource = static_cast<LLGI::BufferDX12*>(readback.get())->Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	dst.PlacedFootprint.Footprint.Width = size.X;
	dst.PlacedFootprint.Footprint.Height = size.Y;
	dst.PlacedFootprint.Footprint.Depth = 1;
	dst.PlacedFootprint.Footprint.RowPitch = rowPitch;
	const UINT z = isArray ? 0 : static_cast<UINT>(layer);
	const D3D12_BOX box{0, 0, z, static_cast<UINT>(size.X), static_cast<UINT>(size.Y), z + 1};
	nativeCommands->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
	nativeTexture->ResourceBarrier(nativeCommands, oldState);
	commands->End();
	graphics->Execute(commands.get());
	graphics->WaitFinish();
	return ReadRows(readback.get(), size, rowPitch);
}
#endif

#ifdef ENABLE_VULKAN
std::vector<uint8_t> ReadVulkan(LLGI::Graphics* graphics, LLGI::Texture* texture, int32_t mip, int32_t layer,
							  const LLGI::Vec2I& size, bool isArray)
{
	const int32_t rowPitch = size.X * 4;
	auto readback = LLGI::CreateSharedPtr(graphics->CreateBuffer(LLGI::BufferUsageType::CopyDst | LLGI::BufferUsageType::MapRead,
															 rowPitch * size.Y));
	VERIFY(readback != nullptr);
	auto pool = LLGI::CreateSharedPtr(graphics->CreateSingleFrameMemoryPool(1024, 1));
	VERIFY(pool != nullptr);
	auto commands = LLGI::CreateSharedPtr(graphics->CreateCommandList(pool.get()));
	VERIFY(commands != nullptr);
	commands->Begin();
	auto nativeCommands = static_cast<LLGI::CommandListVulkan*>(commands.get())->GetCommandBuffer();
	auto nativeTexture = static_cast<LLGI::TextureVulkan*>(texture);
	const auto oldLayout = nativeTexture->GetImageLayouts().at(mip);
	nativeTexture->ResourceBarrier(mip, nativeCommands, vk::ImageLayout::eTransferSrcOptimal);
	vk::BufferImageCopy region{};
	region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.imageSubresource.mipLevel = mip;
	region.imageSubresource.baseArrayLayer = isArray ? layer : 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = vk::Offset3D(0, 0, isArray ? 0 : layer);
	region.imageExtent = vk::Extent3D(size.X, size.Y, 1);
	nativeCommands.copyImageToBuffer(nativeTexture->GetImage(), vk::ImageLayout::eTransferSrcOptimal,
									static_cast<LLGI::BufferVulkan*>(readback.get())->GetBuffer(), region);
	nativeTexture->ResourceBarrier(mip, nativeCommands, oldLayout);
	vk::MemoryBarrier hostBarrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
	nativeCommands.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {},
								   1, &hostBarrier, 0, nullptr, 0, nullptr);
	commands->End();
	graphics->Execute(commands.get());
	graphics->WaitFinish();
	return ReadRows(readback.get(), size, rowPitch);
}
#endif

} // namespace

std::vector<uint8_t> ReadTextureUploadPlane(LLGI::Graphics* graphics, LLGI::DeviceType device, LLGI::Texture* texture,
										  int32_t mip, int32_t layer, const LLGI::Vec2I& size, bool isArray)
{
	switch (device)
	{
#ifdef ENABLE_METAL
	case LLGI::DeviceType::Metal:
		return ReadTextureUploadPlaneMetal(texture, mip, layer, size, isArray);
#endif
#ifdef _WIN32
	case LLGI::DeviceType::DirectX12:
		return ReadDX12(graphics, texture, mip, layer, size, isArray);
#endif
#ifdef ENABLE_VULKAN
	case LLGI::DeviceType::Vulkan:
		return ReadVulkan(graphics, texture, mip, layer, size, isArray);
#endif
#ifdef ENABLE_WEBGPU
	case LLGI::DeviceType::WebGPU:
		return ReadTextureUploadPlaneWebGPU(graphics, texture, mip, layer, size);
#endif
	default:
		std::cerr << "Texture upload readback is not implemented for the selected backend." << std::endl;
		VERIFY(false);
		return {};
	}
}
