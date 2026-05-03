#include "LLGI.CommandListWebGPU.h"
#include "LLGI.BufferWebGPU.h"
#include "LLGI.PipelineStateWebGPU.h"
#include "LLGI.RenderPassWebGPU.h"
#include "LLGI.TextureWebGPU.h"

#include <algorithm>

namespace LLGI
{

CommandListWebGPU::CommandListWebGPU(wgpu::Device device) : device_(device)
{
	wgpu::TextureDescriptor fallbackTextureDesc{};
	fallbackTextureDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
	fallbackTextureDesc.dimension = wgpu::TextureDimension::e2D;
	fallbackTextureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
	fallbackTextureDesc.mipLevelCount = 1;
	fallbackTextureDesc.sampleCount = 1;
	fallbackTextureDesc.size.width = 1;
	fallbackTextureDesc.size.height = 1;
	fallbackTextureDesc.size.depthOrArrayLayers = 1;
	fallbackTexture_ = device_.CreateTexture(&fallbackTextureDesc);
	fallbackTextureView_ = fallbackTexture_.CreateView();

	const uint8_t fallbackTexel[4] = {255, 255, 255, 255};
	wgpu::TexelCopyTextureInfo fallbackDst{};
	fallbackDst.texture = fallbackTexture_;
	fallbackDst.aspect = wgpu::TextureAspect::All;
	wgpu::TexelCopyBufferLayout fallbackLayout{};
	fallbackLayout.bytesPerRow = 4;
	wgpu::Extent3D fallbackExtent{};
	fallbackExtent.width = 1;
	fallbackExtent.height = 1;
	fallbackExtent.depthOrArrayLayers = 1;
	device_.GetQueue().WriteTexture(&fallbackDst, fallbackTexel, sizeof(fallbackTexel), &fallbackLayout, &fallbackExtent);

	for (int w = 0; w < 3; w++)
	{
		for (int f = 0; f < 2; f++)
		{
			std::array<wgpu::FilterMode, 2> filters;
			filters[0] = wgpu::FilterMode::Nearest;
			filters[1] = wgpu::FilterMode::Linear;

			std::array<wgpu::AddressMode, 3> am;
			am[0] = wgpu::AddressMode::ClampToEdge;
			am[1] = wgpu::AddressMode::Repeat;
			am[2] = wgpu::AddressMode::MirrorRepeat;

			wgpu::SamplerDescriptor samplerDesc;

			samplerDesc.magFilter = filters[f];
			samplerDesc.minFilter = filters[f];
			samplerDesc.mipmapFilter = filters[f] == wgpu::FilterMode::Linear ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest;
			samplerDesc.lodMinClamp = 0.0f;
			samplerDesc.lodMaxClamp = 32.0f;
			samplerDesc.maxAnisotropy = 1;
			samplerDesc.addressModeU = am[w];
			samplerDesc.addressModeV = am[w];
			samplerDesc.addressModeW = am[w];
			samplers_[w][f] = device.CreateSampler(&samplerDesc);
		}
	}
}

void CommandListWebGPU::Begin()
{
	wgpu::CommandEncoderDescriptor desc = {};
	commandEncorder_ = device_.CreateCommandEncoder(&desc);

	CommandList::Begin();
}

void CommandListWebGPU::End()
{
	commandBuffer_ = commandEncorder_.Finish();
	commandEncorder_ = nullptr;

	CommandList::End();
}

void CommandListWebGPU::BeginRenderPass(RenderPass* renderPass)
{
	EndComputePass();

	auto rp = static_cast<RenderPassWebGPU*>(renderPass);
	rp->RefreshDescriptor();
	const auto& desc = rp->GetDescriptor();

	renderPassEncorder_ = commandEncorder_.BeginRenderPass(&desc);
	renderPassEncorder_.SetViewport(0.0f, 0.0f, static_cast<float>(rp->GetScreenSize().X), static_cast<float>(rp->GetScreenSize().Y), 0.0f, 1.0f);

	CommandList::BeginRenderPass(renderPass);
}

void CommandListWebGPU::EndRenderPass()
{
	if (renderPassEncorder_ != nullptr)
	{
		renderPassEncorder_.End();
		renderPassEncorder_ = nullptr;
	}
	CommandList::EndRenderPass();
}

void CommandListWebGPU::BeginComputePass()
{
	if (computePassEncorder_ != nullptr)
	{
		return;
	}

	wgpu::ComputePassDescriptor desc{};
	computePassEncorder_ = commandEncorder_.BeginComputePass(&desc);
}

void CommandListWebGPU::EndComputePass()
{
	if (computePassEncorder_ != nullptr)
	{
		computePassEncorder_.End();
		computePassEncorder_ = nullptr;
	}
}

void CommandListWebGPU::Draw(int32_t primitiveCount, int32_t instanceCount)
{
	BindingVertexBuffer bvb;
	BindingIndexBuffer bib;
	PipelineState* bpip = nullptr;

	bool isVBDirtied = false;
	bool isIBDirtied = false;
	bool isPipDirtied = false;

	GetCurrentVertexBuffer(bvb, isVBDirtied);
	GetCurrentIndexBuffer(bib, isIBDirtied);
	GetCurrentPipelineState(bpip, isPipDirtied);

	assert(bvb.vertexBuffer != nullptr);
	assert(bib.indexBuffer != nullptr);
	assert(bpip != nullptr);

	auto vb = static_cast<BufferWebGPU*>(bvb.vertexBuffer);
	auto ib = static_cast<BufferWebGPU*>(bib.indexBuffer);
	auto pip = static_cast<PipelineStateWebGPU*>(bpip);

	if (vb != nullptr)
	{
		renderPassEncorder_.SetVertexBuffer(0, vb->GetBuffer(), bvb.offset, bvb.vertexBuffer->GetSize() - bvb.offset);
	}

	if (ib != nullptr)
	{
		const auto format = bib.stride == 2 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
		renderPassEncorder_.SetIndexBuffer(ib->GetBuffer(), format, bib.offset, ib->GetSize() - bib.offset);
	}

	if (pip != nullptr)
	{
		renderPassEncorder_.SetPipeline(pip->GetRenderPipeline());
		renderPassEncorder_.SetStencilReference(pip->StencilRef);
	}

	std::vector<wgpu::BindGroupEntry> constantBindGroupEntries;

	for (size_t unit_ind = 0; unit_ind < constantBuffers_.size(); unit_ind++)
	{
		auto cb = static_cast<BufferWebGPU*>(constantBuffers_[unit_ind]);
		if (cb == nullptr)
		{
			continue;
		}
		if (!pip->HasBinding(0, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::UniformBuffer))
		{
			continue;
		}

		wgpu::BindGroupEntry entry = {};
		entry.binding = static_cast<uint32_t>(unit_ind);
		entry.buffer = cb->GetBuffer();
		entry.size = cb->GetAllocatedSize() - cb->GetOffset();
		entry.offset = cb->GetOffset();
		constantBindGroupEntries.push_back(entry);
	}

	if (!constantBindGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor constantBindGroupDesc = {};
		constantBindGroupDesc.layout = pip->GetRenderPipeline().GetBindGroupLayout(0);
		constantBindGroupDesc.entries = constantBindGroupEntries.data();
		constantBindGroupDesc.entryCount = constantBindGroupEntries.size();
		auto constantBindGroup = device_.CreateBindGroup(&constantBindGroupDesc);
		renderPassEncorder_.SetBindGroup(0, constantBindGroup);
	}

	std::vector<wgpu::BindGroupEntry> textureGroupEntries;
	std::vector<wgpu::BindGroupEntry> samplerGroupEntries;

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(currentTextures_.size()); unit_ind++)
	{
		if (!pip->HasBinding(1, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::Texture))
			continue;
		auto texture = static_cast<TextureWebGPU*>(currentTextures_[unit_ind].texture);
		auto wm = (int32_t)currentTextures_[unit_ind].wrapMode;
		auto mm = (int32_t)currentTextures_[unit_ind].minMagFilter;

		wgpu::BindGroupEntry textureEntry = {};
		textureEntry.binding = unit_ind;
		textureEntry.textureView = texture != nullptr ? texture->GetTextureView() : fallbackTextureView_;
		textureGroupEntries.push_back(textureEntry);

		wgpu::BindGroupEntry samplerEntry = {};
		if (texture == nullptr || !BitwiseContains(texture->GetParameter().Usage, TextureUsageType::Storage))
		{
			if (!pip->HasBinding(2, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::Sampler))
			{
				continue;
			}
			samplerEntry.binding = unit_ind;
			samplerEntry.sampler = samplers_[wm][mm];
			samplerGroupEntries.push_back(samplerEntry);
		}
	}

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(computeBuffers_.size()); unit_ind++)
	{
		if (computeBuffers_[unit_ind].computeBuffer == nullptr)
		{
			continue;
		}
		if (!pip->HasBinding(1, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::StorageBuffer))
		{
			continue;
		}

		auto buffer = static_cast<BufferWebGPU*>(computeBuffers_[unit_ind].computeBuffer);
		wgpu::BindGroupEntry bufferEntry = {};
		bufferEntry.binding = static_cast<uint32_t>(unit_ind);
		bufferEntry.buffer = buffer->GetBuffer();
		bufferEntry.offset = buffer->GetOffset();
		bufferEntry.size = buffer->GetSize();
		textureGroupEntries.push_back(bufferEntry);
	}

	if (!textureGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor textureBindGroupDesc = {};
		textureBindGroupDesc.layout = pip->GetRenderPipeline().GetBindGroupLayout(1);
		textureBindGroupDesc.entries = textureGroupEntries.data();
		textureBindGroupDesc.entryCount = textureGroupEntries.size();
		auto textureBindGroup = device_.CreateBindGroup(&textureBindGroupDesc);
		renderPassEncorder_.SetBindGroup(1, textureBindGroup);
	}

	if (!samplerGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor samplerBindGroupDesc = {};
		samplerBindGroupDesc.layout = pip->GetRenderPipeline().GetBindGroupLayout(2);
		samplerBindGroupDesc.entries = samplerGroupEntries.data();
		samplerBindGroupDesc.entryCount = samplerGroupEntries.size();
		auto samplerBindGroup = device_.CreateBindGroup(&samplerBindGroupDesc);
		renderPassEncorder_.SetBindGroup(2, samplerBindGroup);
	}

	int indexPerPrim = 0;

	if (pip->Topology == TopologyType::Triangle)
	{
		indexPerPrim = 3;
	}
	else if (pip->Topology == TopologyType::Line)
	{
		indexPerPrim = 2;
	}
	else if (pip->Topology == TopologyType::Point)
	{
		indexPerPrim = 1;
	}
	else
	{
		assert(0);
	}

	renderPassEncorder_.DrawIndexed(primitiveCount * indexPerPrim, instanceCount, 0, 0, 0);
	CommandList::Draw(primitiveCount, instanceCount);
}

void CommandListWebGPU::Dispatch(int32_t groupX, int32_t groupY, int32_t groupZ, int32_t threadX, int32_t threadY, int32_t threadZ)
{
	PipelineState* bpip = nullptr;
	bool isPipDirtied = false;
	GetCurrentPipelineState(bpip, isPipDirtied);
	auto pip = static_cast<PipelineStateWebGPU*>(bpip);
	if (pip == nullptr)
	{
		return;
	}

	if (computePassEncorder_ == nullptr)
	{
		BeginComputePass();
	}

	computePassEncorder_.SetPipeline(pip->GetComputePipeline());

	std::vector<wgpu::BindGroupEntry> constantBindGroupEntries;
	for (size_t unit_ind = 0; unit_ind < constantBuffers_.size(); unit_ind++)
	{
		auto cb = static_cast<BufferWebGPU*>(constantBuffers_[unit_ind]);
		if (cb == nullptr)
		{
			continue;
		}
		if (!pip->HasBinding(0, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::UniformBuffer))
		{
			continue;
		}

		wgpu::BindGroupEntry entry{};
		entry.binding = static_cast<uint32_t>(unit_ind);
		entry.buffer = cb->GetBuffer();
		entry.size = cb->GetAllocatedSize() - cb->GetOffset();
		entry.offset = cb->GetOffset();
		constantBindGroupEntries.push_back(entry);
	}

	if (!constantBindGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor desc{};
		desc.layout = pip->GetComputePipeline().GetBindGroupLayout(0);
		desc.entries = constantBindGroupEntries.data();
		desc.entryCount = constantBindGroupEntries.size();
		auto bindGroup = device_.CreateBindGroup(&desc);
		computePassEncorder_.SetBindGroup(0, bindGroup);
	}

	std::vector<wgpu::BindGroupEntry> textureGroupEntries;
	std::vector<wgpu::BindGroupEntry> samplerAndBufferGroupEntries;

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(currentTextures_.size()); unit_ind++)
	{
		if (!pip->HasBinding(1, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::Texture))
		{
			continue;
		}

		auto texture = static_cast<TextureWebGPU*>(currentTextures_[unit_ind].texture);
		auto wm = (int32_t)currentTextures_[unit_ind].wrapMode;
		auto mm = (int32_t)currentTextures_[unit_ind].minMagFilter;

		wgpu::BindGroupEntry textureEntry{};
		textureEntry.binding = unit_ind;
		textureEntry.textureView = texture != nullptr ? texture->GetTextureView() : fallbackTextureView_;
		textureGroupEntries.push_back(textureEntry);

		if (texture == nullptr || !BitwiseContains(texture->GetParameter().Usage, TextureUsageType::Storage))
		{
			if (!pip->HasBinding(2, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::Sampler))
			{
				continue;
			}
			wgpu::BindGroupEntry samplerEntry{};
			samplerEntry.binding = unit_ind;
			samplerEntry.sampler = samplers_[wm][mm];
			samplerAndBufferGroupEntries.push_back(samplerEntry);
		}
	}

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(computeBuffers_.size()); unit_ind++)
	{
		if (computeBuffers_[unit_ind].computeBuffer == nullptr)
		{
			continue;
		}
		if (!pip->HasBinding(2, static_cast<uint32_t>(unit_ind), ShaderBindingResourceTypeWebGPU::StorageBuffer))
		{
			continue;
		}

		auto buffer = static_cast<BufferWebGPU*>(computeBuffers_[unit_ind].computeBuffer);
		wgpu::BindGroupEntry entry{};
		entry.binding = static_cast<uint32_t>(unit_ind);
		entry.buffer = buffer->GetBuffer();
		entry.offset = buffer->GetOffset();
		entry.size = buffer->GetSize();
		samplerAndBufferGroupEntries.push_back(entry);
	}

	if (!textureGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor desc{};
		desc.layout = pip->GetComputePipeline().GetBindGroupLayout(1);
		desc.entries = textureGroupEntries.data();
		desc.entryCount = textureGroupEntries.size();
		auto bindGroup = device_.CreateBindGroup(&desc);
		computePassEncorder_.SetBindGroup(1, bindGroup);
	}

	if (!samplerAndBufferGroupEntries.empty())
	{
		wgpu::BindGroupDescriptor desc{};
		desc.layout = pip->GetComputePipeline().GetBindGroupLayout(2);
		desc.entries = samplerAndBufferGroupEntries.data();
		desc.entryCount = samplerAndBufferGroupEntries.size();
		auto bindGroup = device_.CreateBindGroup(&desc);
		computePassEncorder_.SetBindGroup(2, bindGroup);
	}

	computePassEncorder_.DispatchWorkgroups(groupX, groupY, groupZ);
	CommandList::Dispatch(groupX, groupY, groupZ, threadX, threadY, threadZ);
}

void CommandListWebGPU::SetScissor(int32_t x, int32_t y, int32_t width, int32_t height)
{
	renderPassEncorder_.SetScissorRect(x, y, width, height);
}

void CommandListWebGPU::CopyTexture(Texture* src, Texture* dst)
{
	auto srcTex = static_cast<TextureWebGPU*>(src);
	CopyTexture(src, dst, {0, 0, 0}, {0, 0, 0}, srcTex->GetParameter().Size, 0, 0);
}

void CommandListWebGPU::CopyTexture(
	Texture* src, Texture* dst, const Vec3I& srcPos, const Vec3I& dstPos, const Vec3I& size, int srcLayer, int dstLayer)
{
	if (isInRenderPass_)
	{
		Log(LogType::Error, "Please call CopyTexture outside of RenderPass");
		return;
	}

	auto srcTex = static_cast<TextureWebGPU*>(src);
	auto dstTex = static_cast<TextureWebGPU*>(dst);

	wgpu::TexelCopyTextureInfo srcTexCopy;
	wgpu::TexelCopyTextureInfo dstTexCopy;
	wgpu::Extent3D extend3d;

	srcTexCopy.texture = srcTex->GetTexture();
	srcTexCopy.origin = {static_cast<uint32_t>(srcPos.X), static_cast<uint32_t>(srcPos.Y), static_cast<uint32_t>(srcLayer + srcPos.Z)};
	srcTexCopy.aspect = wgpu::TextureAspect::All;

	dstTexCopy.texture = dstTex->GetTexture();
	dstTexCopy.origin = {static_cast<uint32_t>(dstPos.X), static_cast<uint32_t>(dstPos.Y), static_cast<uint32_t>(dstLayer + dstPos.Z)};
	dstTexCopy.aspect = wgpu::TextureAspect::All;

	extend3d.width = size.X;
	extend3d.height = size.Y;
	extend3d.depthOrArrayLayers = size.Z;

	commandEncorder_.CopyTextureToTexture(&srcTexCopy, &dstTexCopy, &extend3d);
}

void CommandListWebGPU::CopyBuffer(Buffer* src, Buffer* dst)
{
	auto srcBuffer = static_cast<BufferWebGPU*>(src);
	auto dstBuffer = static_cast<BufferWebGPU*>(dst);
	if (srcBuffer == nullptr || dstBuffer == nullptr)
	{
		return;
	}

	commandEncorder_.CopyBufferToBuffer(srcBuffer->GetBuffer(), 0, dstBuffer->GetBuffer(), 0, std::min(srcBuffer->GetSize(), dstBuffer->GetSize()));
}

void CommandListWebGPU::WaitUntilCompleted()
{
#if !defined(__EMSCRIPTEN__)
	if (device_ != nullptr)
	{
		device_.Tick();
	}
#endif
}

} // namespace LLGI
