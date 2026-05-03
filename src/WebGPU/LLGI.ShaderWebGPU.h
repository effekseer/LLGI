#pragma once

#include "LLGI.BaseWebGPU.h"
#include "../LLGI.Shader.h"
#include <vector>

namespace LLGI
{

enum class ShaderBindingResourceTypeWebGPU
{
	Unknown,
	UniformBuffer,
	Texture,
	Sampler,
	StorageBuffer,
};

struct ShaderBindingWebGPU
{
	uint32_t Group = 0;
	uint32_t Binding = 0;
	ShaderBindingResourceTypeWebGPU ResourceType = ShaderBindingResourceTypeWebGPU::Unknown;
};

class ShaderWebGPU : public Shader
{
private:
	wgpu::ShaderModule shaderModule_;
	std::vector<ShaderBindingWebGPU> bindings_;
	bool hasBindingReflection_ = false;

public:
	ShaderWebGPU();
	~ShaderWebGPU() override;

	bool Initialize(wgpu::Device& device, DataStructure* data, int32_t count);

	wgpu::ShaderModule& GetShaderModule();
	const std::vector<ShaderBindingWebGPU>& GetBindings() const;
	bool HasBindingReflection() const;
};

}
