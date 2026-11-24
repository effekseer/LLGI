#pragma once

#include "../LLGI.PipelineState.h"
#include "LLGI.BaseWebGPU.h"
#include "LLGI.ShaderWebGPU.h"
#include <array>
#include <vector>

namespace LLGI
{

class PipelineStateWebGPU : public PipelineState
{
	std::array<Shader*, static_cast<int>(ShaderStageType::Max)> shaders_;

	wgpu::Device device_;

	wgpu::RenderPipeline renderPipeline_;
	wgpu::ComputePipeline computePipeline_;
	std::array<wgpu::BindGroupLayout, 3> bindGroupLayouts_;
	std::array<size_t, 3> bindingCounts_{};
	std::array<bool, TextureSlotMax> comparisonSamplers_{};
	std::array<bool, TextureSlotMax> whiteFallbackSlots_{};
	std::vector<ShaderBindingWebGPU> bindings_;
	bool hasBindingReflection_ = false;
	bool CreatePipeline();
	bool CacheBindGroupLayouts();

public:
	PipelineStateWebGPU(wgpu::Device device);
	~PipelineStateWebGPU() override;

	void SetShader(ShaderStageType stage, Shader* shader) override;

	// In a browser without suspension Compile returns the immediate creation result.
	bool Compile() override;
	// Returns the immediate creation result and delivers the GPU validation result to the callback.
	bool CompileAsync(std::function<void(bool)> onValidated);

	const wgpu::RenderPipeline& GetRenderPipeline() { return renderPipeline_; }
	wgpu::ComputePipeline GetComputePipeline() { return computePipeline_; }
	wgpu::BindGroupLayout GetBindGroupLayout(uint32_t index) const;
	size_t GetBindingCount(uint32_t group) const { return bindingCounts_[group]; }
	bool UsesComparisonSampler(uint32_t unit) const { return comparisonSamplers_[unit]; }
	bool SupportsWhiteFallback(uint32_t unit) const { return whiteFallbackSlots_[unit]; }
	bool HasBindingReflection() const { return hasBindingReflection_; }
	bool HasBinding(uint32_t group, uint32_t binding) const;
	bool HasBinding(uint32_t group, uint32_t binding, ShaderResourceType resourceType) const;
	bool HasBinding(uint32_t group, uint32_t binding, ShaderResourceType resourceType, ShaderResourceAccess access) const;
};

} // namespace LLGI
