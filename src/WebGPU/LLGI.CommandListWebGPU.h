#pragma once

#include "../LLGI.CommandList.h"
#include "LLGI.BaseWebGPU.h"
#include "LLGI.CompletionWebGPU.h"
#include <array>
#include <vector>

namespace LLGI
{

class PipelineStateWebGPU;

class CommandListWebGPU : public CommandList
{
	struct BindGroupCache
	{
		wgpu::BindGroupLayout layout;
		std::vector<wgpu::BindGroupEntry> entries;
		wgpu::BindGroup bindGroup;
		uint32_t lastRecording = 0;
		bool Matches(const wgpu::BindGroupDescriptor& desc) const;
	};

	struct BindGroupBucket
	{
		std::vector<BindGroupCache> caches;
		size_t next = 0;
	};

	WebGPU::Completion completion_;
	wgpu::Device device_;
	wgpu::CommandBuffer commandBuffer_;
	wgpu::CommandEncoder commandEncoder_;
	wgpu::RenderPassEncoder renderPassEncoder_;
	wgpu::ComputePassEncoder computePassEncoder_;
	wgpu::Sampler samplers_[3][2];
	wgpu::Sampler comparisonSamplers_[3][2];
	wgpu::TextureView fallbackTextureView_;
	std::array<BindGroupBucket, 128> bindGroupBuckets_;
	static constexpr uint32_t RetainedRecordings = 3;
	uint32_t recording_ = 0;
	std::array<std::vector<wgpu::BindGroupEntry>, 3> bindGroupEntries_;

	// Only cache encoder state while LLGI has exclusive control of the render pass.
	bool exclusivePass_ = false;
	wgpu::RenderPipeline boundPipeline_;
	int stencilReference_ = -1;
	std::array<wgpu::BindGroup, 3> boundGroups_;
	void ResetRenderState(bool exclusive);
	enum class PassType
	{
		Render,
		Compute
	};
	bool BindResources(PipelineStateWebGPU& pipeline, PassType pass);
	void ClearBindGroupEntries();
	void FinishRecording();
	const wgpu::Sampler& GetSampler(size_t wrap, size_t filter, bool comparison);
	wgpu::BindGroup GetBindGroup(const wgpu::BindGroupDescriptor& desc);
	bool PrepareStandaloneCommand(const char* commandName);

public:
	explicit CommandListWebGPU(wgpu::Device device);

	void Begin() override;

	bool BeginWithPlatform(void* platformContextPtr) override;

	void End() override;

	void EndWithPlatform() override;

	bool SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f) override;
	void BeginRenderPass(RenderPass* renderPass) override;

	void EndRenderPass() override;

	bool BeginRenderPassWithPlatformPtr(void* platformPtr) override;
	bool BeginRenderPassWithPlatformPtr(void* platformPtr, bool exclusive);

	bool EndRenderPassWithPlatformPtr() override;

	void Draw(int32_t primitiveCount, int32_t instanceCount) override;

	void BeginComputePass() override;

	void EndComputePass() override;

	void Dispatch(int32_t groupX, int32_t groupY, int32_t groupZ, int32_t threadX, int32_t threadY, int32_t threadZ) override;

	void SetTexture(Texture* texture, TextureWrapMode wrapMode, TextureMinMagFilter filter, int32_t unit) override
	{
		SetTextureBinding(texture, wrapMode, filter, unit, NumTexture);
	}

	void SetScissor(int32_t x, int32_t y, int32_t width, int32_t height) override;

	void CopyTexture(Texture* src, Texture* dst) override;

	void CopyTexture(
		Texture* src, Texture* dst, const Vec3I& srcPos, const Vec3I& dstPos, const Vec3I& size, int srcLayer, int dstLayer) override;

	void GenerateMipMap(Texture* src) override;

	void CopyBuffer(Buffer* src, Buffer* dst) override;

	// With no tracked submission, wait for the device queue through this call (including externally submitted work).
	void WaitUntilCompleted() override;
	bool TryGetCompleted(bool& completed) const override;
	// Call immediately after a host submission to enable nonblocking completion queries.
	void TrackSubmission();

	const wgpu::CommandBuffer& GetCommandBuffer() const { return commandBuffer_; }
};

} // namespace LLGI
