#include "LLGI.TextureWebGPU.h"

#include <chrono>
#include <cstring>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace LLGI
{

namespace
{
uint32_t AlignTo(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}
} // namespace

bool TextureWebGPU::Initialize(wgpu::Device& device, const TextureParameter& parameter, wgpu::Instance instance)
{
	device_ = device;
	instance_ = instance;
	parameter_ = parameter;

	const auto getDimension = [](int dimension)
	{
		if (dimension == 1)
			return wgpu::TextureDimension::e1D;

		if (dimension == 2)
			return wgpu::TextureDimension::e2D;

		if (dimension == 3)
			return wgpu::TextureDimension::e3D;

		throw "Not implemented";
	};

	const auto getViewDimension = [](int dimension)
	{
		if (dimension == 1)
			return wgpu::TextureViewDimension::e1D;

		if (dimension == 2)
			return wgpu::TextureViewDimension::e2D;

		if (dimension == 3)
			return wgpu::TextureViewDimension::e3D;

		throw "Not implemented";
	};

	{
		wgpu::TextureDescriptor texDesc{};

		texDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
		if ((parameter.Usage & TextureUsageType::RenderTarget) != TextureUsageType::NoneFlag)
		{
			texDesc.usage |= wgpu::TextureUsage::RenderAttachment;
		}

		if (BitwiseContains(parameter.Usage, TextureUsageType::Storage))
		{
			texDesc.usage |= wgpu::TextureUsage::StorageBinding;
		}

		if ((parameter.Usage & TextureUsageType::External) != TextureUsageType::NoneFlag)
		{
			throw "Not implemented";
			// texDesc.usage |= dawn::platform::kPresentTextureUsage;
		}

		bool isArray = false;
		if ((parameter.Usage & TextureUsageType::Array) != TextureUsageType::NoneFlag)
		{
			isArray = true;
		}

		texDesc.dimension = getDimension(parameter.Dimension);
		texDesc.format = ConvertFormat(parameter.Format);
		texDesc.mipLevelCount = parameter.MipLevelCount;
		texDesc.sampleCount = parameter.SampleCount;
		texDesc.size.width = parameter.Size.X;
		texDesc.size.height = parameter.Size.Y;
		texDesc.size.depthOrArrayLayers = parameter.Size.Z;

		texture_ = device.CreateTexture(&texDesc);
		if (texture_ == nullptr)
		{
			return false;
		}

		wgpu::TextureViewDescriptor texViewDesc{};
		texViewDesc.format = texDesc.format;
		texViewDesc.dimension = isArray && parameter.Dimension == 2 ? wgpu::TextureViewDimension::e2DArray : getViewDimension(parameter.Dimension);
		texViewDesc.baseMipLevel = 0;
		texViewDesc.mipLevelCount = texDesc.mipLevelCount;
		texViewDesc.baseArrayLayer = 0;
		texViewDesc.arrayLayerCount = isArray ? parameter.Size.Z : 1;
		texViewDesc.aspect = wgpu::TextureAspect::All;

		textureView_ = texture_.CreateView(&texViewDesc);
	}

	format_ = parameter.Format;
	usage_ = parameter.Usage;
	samplingCount_ = parameter.SampleCount;
	mipmapCount_ = parameter.MipLevelCount;
	type_ = TextureType::Color;
	if (IsDepthFormat(parameter.Format))
	{
		type_ = TextureType::Depth;
	}
	else if (BitwiseContains(parameter.Usage, TextureUsageType::RenderTarget))
	{
		type_ = TextureType::Render;
	}

	return texture_ != nullptr && textureView_ != nullptr;
}

bool TextureWebGPU::InitializeFromSurfaceTexture(wgpu::Device& device, wgpu::Texture texture, const TextureParameter& parameter)
{
	device_ = device;
	parameter_ = parameter;
	texture_ = texture;
	if (texture_ == nullptr)
	{
		return false;
	}

	wgpu::TextureViewDescriptor texViewDesc{};
	texViewDesc.format = ConvertFormat(parameter.Format);
	texViewDesc.dimension = wgpu::TextureViewDimension::e2D;
	texViewDesc.baseMipLevel = 0;
	texViewDesc.mipLevelCount = 1;
	texViewDesc.baseArrayLayer = 0;
	texViewDesc.arrayLayerCount = 1;
	texViewDesc.aspect = wgpu::TextureAspect::All;
	textureView_ = texture_.CreateView(&texViewDesc);

	format_ = parameter.Format;
	usage_ = parameter.Usage;
	samplingCount_ = parameter.SampleCount;
	mipmapCount_ = parameter.MipLevelCount;
	type_ = TextureType::Screen;

	return textureView_ != nullptr;
}

void* TextureWebGPU::Lock()
{
	auto cpuMemorySize = GetTextureMemorySize(format_, parameter_.Size);
	temp_buffer_.resize(cpuMemorySize);
	return temp_buffer_.data();
}

void TextureWebGPU::Unlock()
{
	wgpu::TexelCopyTextureInfo imageCopyTexture{};
	imageCopyTexture.texture = texture_;
	imageCopyTexture.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferLayout textureDataLayout;
	textureDataLayout.bytesPerRow = GetTextureRowPitch(format_, parameter_.Size);
	wgpu::Extent3D extent;
	extent.width = parameter_.Size.X;
	extent.height = parameter_.Size.Y;
	extent.depthOrArrayLayers = parameter_.Size.Z;
	device_.GetQueue().WriteTexture(&imageCopyTexture, temp_buffer_.data(), temp_buffer_.size(), &textureDataLayout, &extent);
}

bool TextureWebGPU::GetData(std::vector<uint8_t>& data)
{
	const auto bytesPerRowUnaligned = static_cast<uint32_t>(GetTextureRowPitch(format_, parameter_.Size));
	const auto bytesPerRow = AlignTo(bytesPerRowUnaligned, 256);
	const auto height = static_cast<uint32_t>(parameter_.Size.Y);
	const auto depth = static_cast<uint32_t>(parameter_.Size.Z);
	const auto bufferSize = static_cast<uint64_t>(bytesPerRow) * height * depth;

	wgpu::BufferDescriptor bufferDesc{};
	bufferDesc.size = bufferSize;
	bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	auto readbackBuffer = device_.CreateBuffer(&bufferDesc);

	wgpu::CommandEncoderDescriptor encoderDesc{};
	auto encoder = device_.CreateCommandEncoder(&encoderDesc);

	wgpu::TexelCopyTextureInfo src{};
	src.texture = texture_;
	src.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferInfo dst{};
	dst.buffer = readbackBuffer;
	dst.layout.bytesPerRow = bytesPerRow;
	dst.layout.rowsPerImage = height;

	wgpu::Extent3D extent{};
	extent.width = static_cast<uint32_t>(parameter_.Size.X);
	extent.height = height;
	extent.depthOrArrayLayers = depth;
	encoder.CopyTextureToBuffer(&src, &dst, &extent);

	auto commandBuffer = encoder.Finish();
	device_.GetQueue().Submit(1, &commandBuffer);

	bool completed = false;
	bool succeeded = false;
	auto future = readbackBuffer.MapAsync(wgpu::MapMode::Read,
										  0,
										  bufferSize,
#if defined(__EMSCRIPTEN__)
										  wgpu::CallbackMode::AllowSpontaneous,
#else
										  instance_ != nullptr ? wgpu::CallbackMode::WaitAnyOnly : wgpu::CallbackMode::AllowProcessEvents,
#endif
										  [&completed, &succeeded](wgpu::MapAsyncStatus status, wgpu::StringView) {
											  succeeded = status == wgpu::MapAsyncStatus::Success;
											  completed = true;
										  });

	if (instance_ != nullptr)
	{
		instance_.WaitAny(future, 5ULL * 1000ULL * 1000ULL * 1000ULL);
	}
	else
	{
#if defined(__EMSCRIPTEN__)
		const double waitStart = emscripten_get_now();
		while (!completed)
		{
			emscripten_sleep(1);
			if (emscripten_get_now() - waitStart > 5000.0)
			{
				break;
			}
		}
#else
		const auto waitStart = std::chrono::steady_clock::now();
		while (!completed)
		{
			device_.Tick();
			if (std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(5))
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
#endif
	}

	if (!succeeded)
	{
		return false;
	}

	data.resize(static_cast<size_t>(bytesPerRowUnaligned) * height * depth);
	const auto mapped = static_cast<const uint8_t*>(readbackBuffer.GetConstMappedRange(0, bufferSize));
	for (uint32_t z = 0; z < depth; z++)
	{
		for (uint32_t y = 0; y < height; y++)
		{
			memcpy(data.data() + (static_cast<size_t>(z) * height + y) * bytesPerRowUnaligned,
				   mapped + (static_cast<size_t>(z) * height + y) * bytesPerRow,
				   bytesPerRowUnaligned);
		}
	}
	readbackBuffer.Unmap();
	return true;
}

Vec2I TextureWebGPU::GetSizeAs2D() const { return Vec2I(parameter_.Size.X, parameter_.Size.Y); }

bool TextureWebGPU::IsRenderTexture() const
{
	return type_ == TextureType::Render || type_ == TextureType::Screen;
}

bool TextureWebGPU::IsDepthTexture() const { return type_ == TextureType::Depth; }

} // namespace LLGI
