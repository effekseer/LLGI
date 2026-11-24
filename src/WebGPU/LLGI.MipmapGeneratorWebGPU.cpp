#include "LLGI.MipmapGeneratorWebGPU.h"

namespace LLGI::WebGPU
{
namespace
{
const char* MipmapShaderWGSL = R"(
struct VSOutput {
	@builtin(position) position : vec4f,
	@location(0) uv : vec2f,
	@location(1) @interpolate(flat) slice : u32,
};

@vertex
fn VSMain(@builtin(vertex_index) vertexId : u32, @builtin(instance_index) slice : u32) -> VSOutput {
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
	output.slice = slice;
	return output;
}

@group(0) @binding(0) var srcSampler : sampler;
@group(0) @binding(1) var srcTexture : texture_2d<f32>;

@group(0) @binding(1) var volume : texture_3d<f32>;

@fragment
fn Filter2D(input : VSOutput) -> @location(0) vec4f {
	return textureSampleLevel(srcTexture, srcSampler, input.uv, 0.0);
}

fn volumeUV(input : VSOutput) -> vec3f {
	let dstDepth = max(textureDimensions(volume).z / 2u, 1u);
	return vec3f(input.uv, (f32(input.slice) + 0.5) / f32(dstDepth));
}

@fragment
fn Filter3D(input : VSOutput) -> @location(0) vec4f {
	return textureSampleLevel(volume, srcSampler, volumeUV(input), 0.0);
}

@fragment
fn Load2D(input : VSOutput) -> @location(0) vec4f {
	let size = vec2i(textureDimensions(srcTexture));
	let pos = input.uv * vec2f(size) - 0.5;
	let base = vec2i(floor(pos));
	let weight = fract(pos);
	var value = vec4f(0);
	for (var y = 0; y < 2; y++) {
		for (var x = 0; x < 2; x++) {
			let coord = clamp(base + vec2i(x, y), vec2i(0), size - 1);
			let w = select(1.0 - weight, weight, vec2i(x, y) == vec2i(1));
			value += textureLoad(srcTexture, coord, 0) * w.x * w.y;
		}
	}
	return value;
}

@fragment
fn Load3D(input : VSOutput) -> @location(0) vec4f {
	let size = vec3i(textureDimensions(volume));
	let pos = volumeUV(input) * vec3f(size) - 0.5;
	let base = vec3i(floor(pos));
	let weight = fract(pos);
	var value = vec4f(0);
	for (var z = 0; z < 2; z++) {
		for (var y = 0; y < 2; y++) {
			for (var x = 0; x < 2; x++) {
				let corner = vec3i(x, y, z);
				let coord = clamp(base + corner, vec3i(0), size - 1);
				let w = select(1.0 - weight, weight, corner == vec3i(1));
				value += textureLoad(volume, coord, 0) * w.x * w.y * w.z;
			}
		}
	}
	return value;
}
)";

} // namespace

wgpu::RenderPipeline MipmapGenerator::GetPipeline(wgpu::TextureFormat format, bool volume, bool filterable)
{
	std::lock_guard lock(mutex_);
	auto& pipelines = pipelines_[volume ? 1 : 0];
	auto found = pipelines.find(format);
	if (found != pipelines.end())
		return found->second;

	if (!sampler_)
	{
		wgpu::SamplerDescriptor desc{};
		desc.magFilter = wgpu::FilterMode::Linear;
		desc.minFilter = wgpu::FilterMode::Linear;
		sampler_ = device_.CreateSampler(&desc);
	}
	if (!shader_)
	{
		wgpu::ShaderSourceWGSL source{};
		source.code = MipmapShaderWGSL;
		wgpu::ShaderModuleDescriptor desc{};
		desc.nextInChain = &source;
		shader_ = device_.CreateShaderModule(&desc);
	}
	if (!shader_ || !sampler_)
		return nullptr;

	wgpu::ColorTargetState target{};
	target.format = format;
	wgpu::FragmentState fragment{};
	fragment.module = shader_;
	fragment.entryPoint = volume ? (filterable ? "Filter3D" : "Load3D") : (filterable ? "Filter2D" : "Load2D");
	fragment.targetCount = 1;
	fragment.targets = &target;
	wgpu::RenderPipelineDescriptor desc{};
	desc.vertex.module = shader_;
	desc.vertex.entryPoint = "VSMain";
	desc.fragment = &fragment;
	auto pipeline = device_.CreateRenderPipeline(&desc);
	if (pipeline)
		pipelines.emplace(format, pipeline);
	return pipeline;
}

bool MipmapGenerator::Generate(wgpu::CommandEncoder& commandEncoder, const wgpu::Texture& texture, const TextureParameter& parameter)
{
	const auto format = ConvertFormat(parameter.Format);
	const bool volume = parameter.Dimension == 3;
	const bool float32 = parameter.Format == TextureFormatType::R32_FLOAT || parameter.Format == TextureFormatType::R32G32_FLOAT ||
						 parameter.Format == TextureFormatType::R32G32B32A32_FLOAT;
	const bool filterable = !float32 || device_.HasFeature(wgpu::FeatureName::Float32Filterable);
	auto pipeline = GetPipeline(format, volume, filterable);
	if (pipeline == nullptr)
	{
		return false;
	}

	auto layout = pipeline.GetBindGroupLayout(0);

	for (uint32_t mipLevel = 1; mipLevel < static_cast<uint32_t>(parameter.MipLevelCount); mipLevel++)
	{
		wgpu::TextureViewDescriptor srcViewDesc{};
		srcViewDesc.baseMipLevel = mipLevel - 1;
		srcViewDesc.mipLevelCount = 1;
		srcViewDesc.usage = wgpu::TextureUsage::TextureBinding;
		auto srcView = texture.CreateView(&srcViewDesc);

		// Render into a separate attachment before copying to the destination mip.
		const auto size = GetTextureMipSize(parameter.Size, mipLevel);
		wgpu::TextureDescriptor desc{};
		desc.format = format;
		desc.size = {static_cast<uint32_t>(size.X), static_cast<uint32_t>(size.Y), 1};
		desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
		auto target = device_.CreateTexture(&desc);
		if (!target)
			return false;
		wgpu::BindGroupEntry bindGroupEntries[2]{};
		bindGroupEntries[0].binding = 0;
		bindGroupEntries[0].sampler = sampler_;
		bindGroupEntries[1].binding = 1;
		bindGroupEntries[1].textureView = srcView;

		wgpu::BindGroupDescriptor bindGroupDesc{};
		bindGroupDesc.layout = layout;
		bindGroupDesc.entryCount = filterable ? 2 : 1;
		bindGroupDesc.entries = filterable ? bindGroupEntries : bindGroupEntries + 1;
		auto bindGroup = device_.CreateBindGroup(&bindGroupDesc);

		wgpu::RenderPassColorAttachment colorAttachment{};
		colorAttachment.view = target.CreateView();
		colorAttachment.loadOp = wgpu::LoadOp::Clear;
		colorAttachment.storeOp = wgpu::StoreOp::Store;

		wgpu::RenderPassDescriptor renderPassDesc{};
		renderPassDesc.colorAttachmentCount = 1;
		renderPassDesc.colorAttachments = &colorAttachment;

		wgpu::TexelCopyTextureInfo src{}, dst{};
		src.texture = target;
		dst.texture = texture;
		dst.mipLevel = mipLevel;
		for (uint32_t slice = 0; slice < static_cast<uint32_t>(size.Z); ++slice)
		{
			auto passEncoder = commandEncoder.BeginRenderPass(&renderPassDesc);
			passEncoder.SetPipeline(pipeline);
			passEncoder.SetBindGroup(0, bindGroup);
			passEncoder.Draw(3, 1, 0, slice);
			passEncoder.End();
			dst.origin.z = slice;
			commandEncoder.CopyTextureToTexture(&src, &dst, &desc.size);
		}
	}

	return true;
}
} // namespace LLGI::WebGPU
