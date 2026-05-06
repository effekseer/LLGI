#include "LLGI.TextureWebGPU.h"

#include <algorithm>
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

const char* MipmapShaderWGSL = R"(
struct VSOutput {
	@builtin(position) position : vec4f,
	@location(0) uv : vec2f,
};

@vertex
fn VSMain(@builtin(vertex_index) vertexId : u32) -> VSOutput {
	var positions = array<vec2f, 3>(
		vec2f(-1.0, -1.0),
		vec2f(-1.0, 3.0),
		vec2f(3.0, -1.0));
	var uvs = array<vec2f, 3>(
		vec2f(0.0, 1.0),
		vec2f(0.0, -1.0),
		vec2f(2.0, 1.0));

	var output : VSOutput;
	output.position = vec4f(positions[vertexId], 0.0, 1.0);
	output.uv = uvs[vertexId];
	return output;
}

@group(0) @binding(0) var srcSampler : sampler;
@group(0) @binding(1) var srcTexture : texture_2d<f32>;

@fragment
fn PSMain(input : VSOutput) -> @location(0) vec4f {
	return textureSample(srcTexture, srcSampler, input.uv);
}
)";

struct MipmapSize
{
	uint32_t Width = 1;
	uint32_t Height = 1;
};

bool CanGenerateMipMaps(const TextureParameter& parameter)
{
	return parameter.MipLevelCount > 1 && parameter.Dimension == 2 && parameter.Size.Z == 1 && !IsDepthFormat(parameter.Format) &&
		   parameter.SampleCount == 1;
}

MipmapSize GetMipmapSize(const TextureParameter& parameter, uint32_t mipLevel)
{
	MipmapSize size;
	size.Width = std::max<uint32_t>(static_cast<uint32_t>(parameter.Size.X) >> mipLevel, 1);
	size.Height = std::max<uint32_t>(static_cast<uint32_t>(parameter.Size.Y) >> mipLevel, 1);
	return size;
}

wgpu::TextureViewDescriptor CreateMipmapViewDesc(wgpu::TextureFormat format, uint32_t mipLevel)
{
	wgpu::TextureViewDescriptor desc{};
	desc.format = format;
	desc.dimension = wgpu::TextureViewDimension::e2D;
	desc.baseMipLevel = mipLevel;
	desc.mipLevelCount = 1;
	desc.baseArrayLayer = 0;
	desc.arrayLayerCount = 1;
	desc.aspect = wgpu::TextureAspect::All;
	return desc;
}

wgpu::RenderPassColorAttachment CreateMipmapColorAttachment(wgpu::TextureView view)
{
	wgpu::RenderPassColorAttachment attachment{};
	attachment.view = view;
	attachment.loadOp = wgpu::LoadOp::Clear;
	attachment.storeOp = wgpu::StoreOp::Store;
	attachment.clearValue = {0, 0, 0, 0};
	return attachment;
}

} // namespace

bool TextureWebGPU::CreateMipmapResources()
{
	if (mipmapShaderModule_ != nullptr)
	{
		return true;
	}

	wgpu::BindGroupLayoutEntry entries[2]{};
	entries[0].binding = 0;
	entries[0].visibility = wgpu::ShaderStage::Fragment;
	entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
	entries[1].binding = 1;
	entries[1].visibility = wgpu::ShaderStage::Fragment;
	entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
	entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
	entries[1].texture.multisampled = false;

	wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = 2;
	bindGroupLayoutDesc.entries = entries;
	mipmapBindGroupLayout_ = device_.CreateBindGroupLayout(&bindGroupLayoutDesc);
	if (mipmapBindGroupLayout_ == nullptr)
	{
		return false;
	}

	wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &mipmapBindGroupLayout_;
	mipmapPipelineLayout_ = device_.CreatePipelineLayout(&pipelineLayoutDesc);
	if (mipmapPipelineLayout_ == nullptr)
	{
		return false;
	}

	wgpu::SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
	samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
	samplerDesc.magFilter = wgpu::FilterMode::Linear;
	samplerDesc.minFilter = wgpu::FilterMode::Linear;
	samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 32.0f;
	samplerDesc.maxAnisotropy = 1;
	mipmapSampler_ = device_.CreateSampler(&samplerDesc);
	if (mipmapSampler_ == nullptr)
	{
		return false;
	}

	wgpu::ShaderSourceWGSL wgslDesc{};
	wgslDesc.code = wgpu::StringView(MipmapShaderWGSL, strlen(MipmapShaderWGSL));
	wgpu::ShaderModuleDescriptor shaderDesc{};
	shaderDesc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&wgslDesc);
	mipmapShaderModule_ = device_.CreateShaderModule(&shaderDesc);
	return mipmapShaderModule_ != nullptr;
}

wgpu::RenderPipeline TextureWebGPU::GetMipmapPipeline(wgpu::TextureFormat format)
{
	auto found = mipmapPipelines_.find(format);
	if (found != mipmapPipelines_.end())
	{
		return found->second;
	}

	if (!CreateMipmapResources())
	{
		return nullptr;
	}

	wgpu::ColorTargetState colorTargetState{};
	colorTargetState.format = format;
	colorTargetState.writeMask = wgpu::ColorWriteMask::All;

	wgpu::FragmentState fragmentState{};
	fragmentState.module = mipmapShaderModule_;
	fragmentState.entryPoint = "PSMain";
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTargetState;

	wgpu::RenderPipelineDescriptor desc{};
	desc.layout = mipmapPipelineLayout_;
	desc.vertex.module = mipmapShaderModule_;
	desc.vertex.entryPoint = "VSMain";
	desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
	desc.primitive.frontFace = wgpu::FrontFace::CW;
	desc.primitive.cullMode = wgpu::CullMode::None;
	desc.fragment = &fragmentState;
	desc.multisample.count = 1;
	desc.multisample.mask = UINT32_MAX;
	desc.multisample.alphaToCoverageEnabled = false;

	auto pipeline = device_.CreateRenderPipeline(&desc);
	if (pipeline != nullptr)
	{
		mipmapPipelines_[format] = pipeline;
	}
	return pipeline;
}

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
		if (IsDepthFormat(parameter.Format) || (parameter.Usage & TextureUsageType::RenderTarget) != TextureUsageType::NoneFlag)
		{
			texDesc.usage |= wgpu::TextureUsage::RenderAttachment;
		}

		if (CanGenerateMipMaps(parameter))
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
	imageCopyTexture.mipLevel = 0;
	imageCopyTexture.aspect = wgpu::TextureAspect::All;

	wgpu::TexelCopyBufferLayout textureDataLayout;
	textureDataLayout.bytesPerRow = GetTextureRowPitch(format_, parameter_.Size);
	wgpu::Extent3D extent;
	extent.width = parameter_.Size.X;
	extent.height = parameter_.Size.Y;
	extent.depthOrArrayLayers = parameter_.Size.Z;
	device_.GetQueue().WriteTexture(&imageCopyTexture, temp_buffer_.data(), temp_buffer_.size(), &textureDataLayout, &extent);
}

void TextureWebGPU::GenerateMipMaps(wgpu::CommandEncoder& commandEncoder)
{
	if (!CanGenerateMipMaps(parameter_))
	{
		return;
	}

	const auto format = ConvertFormat(parameter_.Format);
	auto pipeline = GetMipmapPipeline(format);
	if (pipeline == nullptr)
	{
		return;
	}

	for (uint32_t mipLevel = 1; mipLevel < static_cast<uint32_t>(parameter_.MipLevelCount); mipLevel++)
	{
		const auto srcViewDesc = CreateMipmapViewDesc(format, mipLevel - 1);
		auto srcView = texture_.CreateView(&srcViewDesc);

		const auto dstViewDesc = CreateMipmapViewDesc(format, mipLevel);
		auto dstView = texture_.CreateView(&dstViewDesc);

		wgpu::BindGroupEntry bindGroupEntries[2]{};
		bindGroupEntries[0].binding = 0;
		bindGroupEntries[0].sampler = mipmapSampler_;
		bindGroupEntries[1].binding = 1;
		bindGroupEntries[1].textureView = srcView;

		wgpu::BindGroupDescriptor bindGroupDesc{};
		bindGroupDesc.layout = mipmapBindGroupLayout_;
		bindGroupDesc.entryCount = 2;
		bindGroupDesc.entries = bindGroupEntries;
		auto bindGroup = device_.CreateBindGroup(&bindGroupDesc);

		auto colorAttachment = CreateMipmapColorAttachment(dstView);

		wgpu::RenderPassDescriptor renderPassDesc{};
		renderPassDesc.colorAttachmentCount = 1;
		renderPassDesc.colorAttachments = &colorAttachment;

		auto passEncoder = commandEncoder.BeginRenderPass(&renderPassDesc);
		const auto dstSize = GetMipmapSize(parameter_, mipLevel);
		passEncoder.SetViewport(0.0f, 0.0f, static_cast<float>(dstSize.Width), static_cast<float>(dstSize.Height), 0.0f, 1.0f);
		passEncoder.SetPipeline(pipeline);
		passEncoder.SetBindGroup(0, bindGroup);
		passEncoder.Draw(3);
		passEncoder.End();
	}
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
