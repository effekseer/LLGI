#include "LLGI.TextureWebGPU.h"

#include "LLGI.MipmapGeneratorWebGPU.h"
#include "LLGI.TextureReadbackWebGPU.h"

namespace LLGI
{

namespace
{
bool CanGenerateMipMaps(const TextureParameter& parameter)
{
	return parameter.IsMipmapGenerationEnabled && parameter.MipLevelCount > 1 && (parameter.Dimension == 2 || parameter.Dimension == 3) &&
		   !IsBlockCompressedFormat(parameter.Format) && !IsDepthFormat(parameter.Format) && parameter.SampleCount == 1;
}

void WriteTextureMipLevel(
	wgpu::Device& device, wgpu::Texture texture, TextureFormatType format, uint32_t mipLevel, Vec3I size, const uint8_t* data)
{
	const auto rowPitch = static_cast<uint32_t>(GetTextureRowPitch(format, size));
	wgpu::TexelCopyTextureInfo dst{};
	dst.texture = texture;
	dst.mipLevel = mipLevel;
	dst.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferLayout layout{};
	layout.bytesPerRow = rowPitch;
	layout.rowsPerImage = GetTextureRowCount(format, {size.X, size.Y, 1});

	// Queue writes accept tightly packed rows; the 256-byte row alignment applies to buffer copies.
	const auto extent = GetTextureCopyExtent(format, size);
	device.GetQueue().WriteTexture(&dst, data, GetTextureMemorySize(format, size), &layout, &extent);
}

} // namespace

bool TextureWebGPU::Initialize(wgpu::Device& device, const TextureParameter& parameter, wgpu::Instance instance)
{
	return Initialize(device, parameter, instance, nullptr);
}

bool TextureWebGPU::Initialize(wgpu::Device& device,
							   const TextureParameter& parameter,
							   wgpu::Instance instance,
							   std::shared_ptr<WebGPU::MipmapGenerator> mipmaps)
{
	if (!ValidateTextureParameter(parameter, "TextureWebGPU::Initialize"))
		return false;
	device_ = device;
	instance_ = instance;
	parameter_ = parameter;
	mipmaps_.reset();
	baseMipView_ = nullptr;
	depthOnlyView_ = nullptr;

	if (BitwiseContains(parameter.Usage, TextureUsageType::External))
		throw "Not implemented";

	const bool isArray = BitwiseContains(parameter.Usage, TextureUsageType::Array);
	wgpu::TextureDescriptor desc{};
	wgpu::TextureViewDescriptor view{};
	switch (parameter.Dimension)
	{
	case 1:
		desc.dimension = wgpu::TextureDimension::e1D;
		view.dimension = wgpu::TextureViewDimension::e1D;
		break;
	case 2:
		desc.dimension = wgpu::TextureDimension::e2D;
		view.dimension = isArray ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D;
		break;
	case 3:
		desc.dimension = wgpu::TextureDimension::e3D;
		view.dimension = wgpu::TextureViewDimension::e3D;
		break;
	default:
		throw "Not implemented";
	}

	if (CanGenerateMipMaps(parameter))
		mipmaps_ = mipmaps ? std::move(mipmaps) : std::make_shared<WebGPU::MipmapGenerator>(device);
	desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
	if (IsDepthFormat(parameter.Format) || BitwiseContains(parameter.Usage, TextureUsageType::RenderTarget))
		desc.usage |= wgpu::TextureUsage::RenderAttachment;
	if (BitwiseContains(parameter.Usage, TextureUsageType::Storage))
		desc.usage |= wgpu::TextureUsage::StorageBinding;
	desc.format = ConvertFormat(parameter.Format);
	desc.mipLevelCount = parameter.MipLevelCount;
	desc.sampleCount = parameter.SampleCount;
	desc.size = {static_cast<uint32_t>(parameter.Size.X), static_cast<uint32_t>(parameter.Size.Y), static_cast<uint32_t>(parameter.Size.Z)};
	texture_ = device.CreateTexture(&desc);
	if (!texture_)
		return false;

	view.mipLevelCount = desc.mipLevelCount;
	view.arrayLayerCount = isArray ? parameter.Size.Z : 1;
	textureView_ = texture_.CreateView(&view);
	if (HasStencil(parameter.Format))
	{
		auto sampledView = view;
		sampledView.aspect = wgpu::TextureAspect::DepthOnly;
		sampledView.usage = wgpu::TextureUsage::TextureBinding;
		depthOnlyView_ = texture_.CreateView(&sampledView);
		if (!depthOnlyView_)
			return false;
	}
	if (view.mipLevelCount > 1 &&
		(desc.usage & (wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::StorageBinding)) != wgpu::TextureUsage::None)
	{
		view.mipLevelCount = 1;
		baseMipView_ = texture_.CreateView(&view);
		if (!baseMipView_)
			return false;
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
	mipmaps_.reset();
	baseMipView_ = nullptr;
	depthOnlyView_ = nullptr;
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
	const auto preserveDepth = (parameter_.Usage & TextureUsageType::Array) != TextureUsageType::NoneFlag;
	auto cpuMemorySize = GetTextureMemorySize(format_, parameter_.Size, parameter_.MipLevelCount, preserveDepth);
	uploadData_.resize(cpuMemorySize);
	return uploadData_.data();
}

void TextureWebGPU::Unlock()
{
	const auto preserveDepth = (parameter_.Usage & TextureUsageType::Array) != TextureUsageType::NoneFlag;
	size_t offset = 0;
	for (int32_t mipLevel = 0; mipLevel < (mipmaps_ ? 1 : parameter_.MipLevelCount); mipLevel++)
	{
		auto mipSize = GetTextureMipSize(parameter_.Size, mipLevel, preserveDepth);
		WriteTextureMipLevel(device_, texture_, format_, mipLevel, mipSize, uploadData_.data() + offset);
		offset += GetTextureMemorySize(format_, mipSize);
	}

	if (mipmaps_)
	{
		wgpu::CommandEncoderDescriptor encoderDesc{};
		auto encoder = device_.CreateCommandEncoder(&encoderDesc);
		GenerateMipMaps(encoder);
		auto commandBuffer = encoder.Finish();
		device_.GetQueue().Submit(1, &commandBuffer);
	}
}

void TextureWebGPU::GenerateMipMaps(wgpu::CommandEncoder& commandEncoder)
{
	if (mipmaps_ && !mipmaps_->Generate(commandEncoder, texture_, parameter_))
		Log(LogType::Error, "Failed to generate WebGPU texture mipmaps");
}

bool TextureWebGPU::GetData(std::vector<uint8_t>& data)
{
	return WebGPU::TextureReadback(device_, texture_, format_, parameter_.Size).Read(device_, instance_, data);
}

Vec2I TextureWebGPU::GetSizeAs2D() const { return Vec2I(parameter_.Size.X, parameter_.Size.Y); }

} // namespace LLGI
