#pragma once

#include "LLGI.BaseWebGPU.h"
#include "../LLGI.Shader.h"
#include <vector>

namespace LLGI
{

struct ShaderBindingWebGPU
{
	bool operator==(const ShaderBindingWebGPU& rhs) const;

	uint32_t Group = 0;
	uint32_t Binding = 0;
	ShaderResourceBinding Resource;
	bool ComparisonSampler = false;
	bool SupportsWhiteFallback = false;
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
