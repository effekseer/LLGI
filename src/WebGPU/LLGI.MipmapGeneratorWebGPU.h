#pragma once

#include "../LLGI.Graphics.h"
#include "LLGI.BaseWebGPU.h"
#include <mutex>
#include <unordered_map>

namespace LLGI::WebGPU
{
class MipmapGenerator
{
	wgpu::Device device_;
	wgpu::Sampler sampler_;
	wgpu::ShaderModule shader_;
	std::array<std::unordered_map<wgpu::TextureFormat, wgpu::RenderPipeline>, 2> pipelines_;
	std::mutex mutex_;

	wgpu::RenderPipeline GetPipeline(wgpu::TextureFormat format, bool volume, bool filterable);

public:
	explicit MipmapGenerator(wgpu::Device device) : device_(std::move(device)) {}
	bool Generate(wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, const TextureParameter& parameter);
};
} // namespace LLGI::WebGPU
