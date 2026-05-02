#pragma once

#include "../LLGI.PipelineState.h"
#include "LLGI.BaseWebGPU.h"

namespace LLGI
{

class PipelineStateWebGPU : public PipelineState
{
	std::array<Shader*, static_cast<int>(ShaderStageType::Max)> shaders_;

	wgpu::Device device_;

	wgpu::RenderPipeline renderPipeline_;
	wgpu::ComputePipeline computePipeline_;

public:
	PipelineStateWebGPU(wgpu::Device device);
	~PipelineStateWebGPU() override;

	void SetShader(ShaderStageType stage, Shader* shader) override;

	bool Compile() override;

	wgpu::RenderPipeline GetRenderPipeline() { return renderPipeline_; }
	wgpu::ComputePipeline GetComputePipeline() { return computePipeline_; }
};

} // namespace LLGI
