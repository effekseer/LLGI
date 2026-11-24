#include "LLGI.CommandListWebGPU.h"
#include "LLGI.BufferWebGPU.h"
#include "LLGI.PipelineStateWebGPU.h"
#include "LLGI.RenderPassWebGPU.h"
#include "LLGI.TextureWebGPU.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace LLGI
{
namespace
{
constexpr std::array<wgpu::FilterMode, 2> TextureFilterModes = {
	wgpu::FilterMode::Nearest,
	wgpu::FilterMode::Linear,
};
constexpr std::array<wgpu::AddressMode, 3> TextureAddressModes = {
	wgpu::AddressMode::ClampToEdge,
	wgpu::AddressMode::Repeat,
	wgpu::AddressMode::MirrorRepeat,
};

} // namespace

bool CommandListWebGPU::BindGroupCache::Matches(const wgpu::BindGroupDescriptor& desc) const
{
	if (layout.Get() != desc.layout.Get() || entries.size() != desc.entryCount)
		return false;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const auto& lhs = entries[i];
		const auto& rhs = desc.entries[i];
		if (lhs.binding != rhs.binding || lhs.buffer.Get() != rhs.buffer.Get() || lhs.textureView.Get() != rhs.textureView.Get() ||
			lhs.sampler.Get() != rhs.sampler.Get() || lhs.offset != rhs.offset || lhs.size != rhs.size)
			return false;
	}
	return true;
}

void CommandListWebGPU::ResetRenderState(bool exclusive)
{
	exclusivePass_ = exclusive;
	boundPipeline_ = nullptr;
	stencilReference_ = -1;
	boundGroups_ = {};
}

void CommandListWebGPU::ClearBindGroupEntries()
{
	for (auto& entries : bindGroupEntries_)
		entries.clear();
}

void CommandListWebGPU::FinishRecording()
{
	EndComputePass();
	ClearBindGroupEntries();
	ResetRenderState(false);
	for (auto& bucket : bindGroupBuckets_)
	{
		// Expire groups when the command list rotates out the corresponding resource references.
		std::erase_if(bucket.caches,
					  [this](const BindGroupCache& cache) { return recording_ - cache.lastRecording >= RetainedRecordings; });
	}
	++recording_;
}

wgpu::BindGroup CommandListWebGPU::GetBindGroup(const wgpu::BindGroupDescriptor& desc)
{
	// Select a small bucket, then compare the full descriptors, including offsets and sizes.
	size_t hash = reinterpret_cast<uintptr_t>(desc.layout.Get());
	for (size_t i = 0; i < desc.entryCount; ++i)
	{
		const auto& entry = desc.entries[i];
		const auto handle = reinterpret_cast<uintptr_t>(entry.buffer.Get()) | reinterpret_cast<uintptr_t>(entry.textureView.Get()) |
							reinterpret_cast<uintptr_t>(entry.sampler.Get());
		hash = (hash ^ handle ^ static_cast<size_t>(entry.offset)) * 16777619u;
	}
	hash ^= hash >> 16;
	hash ^= hash >> 8;
	auto& bucket = bindGroupBuckets_[hash % bindGroupBuckets_.size()];
	for (auto& cache : bucket.caches)
	{
		if (cache.Matches(desc))
		{
			cache.lastRecording = recording_;
			return cache.bindGroup;
		}
	}

	// Bound retained resource handles even if every draw uses different buffers or textures.
	constexpr size_t Capacity = 8;
	auto& cache = bucket.caches.size() < Capacity ? bucket.caches.emplace_back() : bucket.caches[bucket.next++ % Capacity];
	cache.lastRecording = recording_;
	cache.layout = desc.layout;
	cache.entries.assign(desc.entries, desc.entries + desc.entryCount);
	cache.bindGroup = device_.CreateBindGroup(&desc);
	return cache.bindGroup;
}

bool CommandListWebGPU::PrepareStandaloneCommand(const char* commandName)
{
	if (renderPassEncoder_)
	{
		Log(LogType::Error, std::string("Please call ") + commandName + " outside of RenderPass");
		return false;
	}

	EndComputePass();
	return true;
}

CommandListWebGPU::CommandListWebGPU(wgpu::Device device) : CommandList(RetainedRecordings), device_(device)
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
	auto fallbackTexture = device_.CreateTexture(&fallbackTextureDesc);
	fallbackTextureView_ = fallbackTexture.CreateView();

	const std::array<uint8_t, 4> white = {255, 255, 255, 255};
	wgpu::TexelCopyTextureInfo destination{};
	destination.texture = fallbackTexture;
	wgpu::TexelCopyBufferLayout layout{};
	device_.GetQueue().WriteTexture(&destination, white.data(), white.size(), &layout, &fallbackTextureDesc.size);
}

const wgpu::Sampler& CommandListWebGPU::GetSampler(size_t wrap, size_t filter, bool comparison)
{
	auto& sampler = comparison ? comparisonSamplers_[wrap][filter] : samplers_[wrap][filter];
	if (!sampler)
	{
		wgpu::SamplerDescriptor desc{};
		desc.magFilter = desc.minFilter = TextureFilterModes[filter];
		desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
		desc.addressModeU = desc.addressModeV = desc.addressModeW = TextureAddressModes[wrap];
		if (comparison)
			desc.compare = wgpu::CompareFunction::LessEqual;
		sampler = device_.CreateSampler(&desc);
	}
	return sampler;
}

void CommandListWebGPU::Begin()
{
	completion_.reset();
	wgpu::CommandEncoderDescriptor desc = {};
	commandEncoder_ = device_.CreateCommandEncoder(&desc);

	CommandList::Begin();
}

bool CommandListWebGPU::BeginWithPlatform(void* platformContextPtr)
{
	if (platformContextPtr == nullptr)
	{
		Begin();
		return true;
	}

	completion_.reset();
	commandBuffer_ = nullptr;
	commandEncoder_ = wgpu::CommandEncoder::Acquire(reinterpret_cast<WGPUCommandEncoder>(platformContextPtr));

	return CommandList::BeginWithPlatform(platformContextPtr);
}

void CommandListWebGPU::End()
{
	FinishRecording();
	commandBuffer_ = commandEncoder_.Finish();
	commandEncoder_ = nullptr;

	CommandList::End();
}

void CommandListWebGPU::EndWithPlatform()
{
	FinishRecording();
	commandEncoder_ = nullptr;
	commandBuffer_ = nullptr;

	CommandList::EndWithPlatform();
}

bool CommandListWebGPU::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
	if (!ValidateViewport(x, y, width, height, minDepth, maxDepth)) return false;
	if (renderPassEncoder_ == nullptr)
		return false;
	renderPassEncoder_.SetViewport(x, y, width, height, minDepth, maxDepth);
	return true;
}

void CommandListWebGPU::BeginRenderPass(RenderPass* renderPass)
{
	EndComputePass();

	auto rp = static_cast<RenderPassWebGPU*>(renderPass);
	rp->RefreshDescriptor();
	const auto& desc = rp->GetDescriptor();

	renderPassEncoder_ = commandEncoder_.BeginRenderPass(&desc);
	renderPassEncoder_.SetViewport(
		0.0f, 0.0f, static_cast<float>(rp->GetScreenSize().X), static_cast<float>(rp->GetScreenSize().Y), 0.0f, 1.0f);

	ResetRenderState(true);
	CommandList::BeginRenderPass(renderPass);
}

void CommandListWebGPU::EndRenderPass()
{
	if (renderPassEncoder_ != nullptr)
	{
		renderPassEncoder_.End();
		renderPassEncoder_ = nullptr;
	}
	CommandList::EndRenderPass();
}

bool CommandListWebGPU::BeginRenderPassWithPlatformPtr(void* platformPtr) { return BeginRenderPassWithPlatformPtr(platformPtr, false); }

bool CommandListWebGPU::BeginRenderPassWithPlatformPtr(void* platformPtr, bool exclusive)
{
	if (platformPtr == nullptr)
	{
		return false;
	}

	EndComputePass();
	renderPassEncoder_ = wgpu::RenderPassEncoder::Acquire(reinterpret_cast<WGPURenderPassEncoder>(platformPtr));

	ResetRenderState(exclusive);
	return CommandList::BeginRenderPassWithPlatformPtr(platformPtr);
}

bool CommandListWebGPU::EndRenderPassWithPlatformPtr()
{
	renderPassEncoder_ = nullptr;
	CommandList::EndRenderPass();
	return true;
}

void CommandListWebGPU::BeginComputePass()
{
	if (renderPassEncoder_ || !commandEncoder_)
	{
		Log(LogType::Error, "BeginComputePass requires a command encoder outside of RenderPass");
		return;
	}
	if (computePassEncoder_ != nullptr)
	{
		return;
	}

	wgpu::ComputePassDescriptor desc{};
	computePassEncoder_ = commandEncoder_.BeginComputePass(&desc);
	CommandList::BeginComputePass();
}

void CommandListWebGPU::EndComputePass()
{
	if (computePassEncoder_ != nullptr)
	{
		computePassEncoder_.End();
		computePassEncoder_ = nullptr;
	}
}

bool CommandListWebGPU::BindResources(PipelineStateWebGPU& pipeline, PassType pass)
{
	ClearBindGroupEntries();
	// The shader ABI places storage buffers with textures in render passes and with samplers in compute passes.
	const uint32_t storageGroup = pass == PassType::Render ? 1 : 2;

	for (size_t unit_ind = 0; unit_ind < constantBuffers_.size(); unit_ind++)
	{
		auto cb = static_cast<BufferWebGPU*>(constantBuffers_[unit_ind]);
		if (cb == nullptr)
		{
			continue;
		}
		if (!pipeline.HasBinding(0, static_cast<uint32_t>(unit_ind), ShaderResourceType::UniformBuffer))
		{
			continue;
		}

		wgpu::BindGroupEntry entry = {};
		entry.binding = static_cast<uint32_t>(unit_ind);
		entry.buffer = cb->GetBuffer();
		entry.size = cb->GetAllocatedSize();
		bindGroupEntries_[0].push_back(entry);
	}

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(currentTextures_.size()); unit_ind++)
	{
		auto texture = static_cast<TextureWebGPU*>(currentTextures_[unit_ind].texture);
		if (!pipeline.HasBindingReflection() && !currentTextures_[unit_ind].isBound)
			continue;
		const bool storage = texture && BitwiseContains(texture->GetUsage(), TextureUsageType::Storage) &&
							 pipeline.HasBinding(1, unit_ind, ShaderResourceType::StorageTexture);
		if (storage || pipeline.HasBinding(1, unit_ind, ShaderResourceType::Texture))
		{
			if (!texture && !pipeline.SupportsWhiteFallback(unit_ind))
			{
				Log(LogType::Error, "Missing required WebGPU texture at slot " + std::to_string(unit_ind));
				return false;
			}
			wgpu::BindGroupEntry entry{};
			entry.binding = unit_ind;
			entry.textureView = storage ? texture->GetBaseMipView() : (texture ? texture->GetSampledView() : fallbackTextureView_);
			bindGroupEntries_[1].push_back(entry);
		}
		if (pipeline.HasBinding(2, unit_ind, ShaderResourceType::Sampler))
		{
			const auto wrap = static_cast<size_t>(currentTextures_[unit_ind].wrapMode);
			const auto filter = static_cast<size_t>(currentTextures_[unit_ind].minMagFilter);
			wgpu::BindGroupEntry entry{};
			entry.binding = unit_ind;
			entry.sampler = GetSampler(wrap, filter, pipeline.UsesComparisonSampler(unit_ind));
			bindGroupEntries_[2].push_back(entry);
		}
	}

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(storageBuffers_.size()); unit_ind++)
	{
		if (storageBuffers_[unit_ind].storageBuffer == nullptr)
		{
			continue;
		}
		if (!pipeline.HasBinding(
				storageGroup, static_cast<uint32_t>(unit_ind), ShaderResourceType::StorageBuffer, storageBuffers_[unit_ind].binding.Access))
		{
			continue;
		}

		auto buffer = static_cast<BufferWebGPU*>(storageBuffers_[unit_ind].storageBuffer);
		wgpu::BindGroupEntry bufferEntry = {};
		bufferEntry.binding = static_cast<uint32_t>(unit_ind);
		bufferEntry.buffer = buffer->GetBuffer();
		bufferEntry.size = buffer->GetSize();
		bindGroupEntries_[storageGroup].push_back(bufferEntry);
	}

	for (uint32_t slot = 0; slot < bindGroupEntries_.size(); ++slot)
	{
		const auto& entries = bindGroupEntries_[slot];
		if (pipeline.HasBindingReflection() && entries.size() != pipeline.GetBindingCount(slot))
		{
			Log(LogType::Error, "Missing required WebGPU resource in group " + std::to_string(slot));
			return false;
		}
		if (entries.empty())
		{
			if (!pipeline.HasBindingReflection())
			{
				if (pass == PassType::Compute)
					computePassEncoder_.SetBindGroup(slot, nullptr);
				else
				{
					renderPassEncoder_.SetBindGroup(slot, nullptr);
					boundGroups_[slot] = nullptr;
				}
			}
			continue;
		}
		wgpu::BindGroupDescriptor desc{};
		desc.layout = pipeline.GetBindGroupLayout(slot);
		desc.entries = entries.data();
		desc.entryCount = entries.size();
		auto group = GetBindGroup(desc);
		if (pass == PassType::Compute)
		{
			computePassEncoder_.SetBindGroup(slot, group);
		}
		else if (!exclusivePass_ || boundGroups_[slot].Get() != group.Get())
		{
			renderPassEncoder_.SetBindGroup(slot, group);
			if (exclusivePass_)
			{
				boundGroups_[slot] = std::move(group);
			}
		}
	}
	return true;
}

void CommandListWebGPU::Draw(int32_t primitiveCount, int32_t instanceCount)
{
	BindingVertexBuffer vertex;
	BindingIndexBuffer index;
	PipelineState* currentPipeline = nullptr;
	bool vertexChanged, indexChanged, pipelineChanged;
	GetCurrentVertexBuffer(vertex, vertexChanged);
	GetCurrentIndexBuffer(index, indexChanged);
	GetCurrentPipelineState(currentPipeline, pipelineChanged);

	if (!renderPassEncoder_ || !vertex.vertexBuffer || !index.indexBuffer || !currentPipeline)
	{
		Log(LogType::Error, "Draw requires a render pass, vertex and index buffers, and a pipeline");
		return;
	}

	if (!exclusivePass_ || vertexChanged)
	{
		auto buffer = static_cast<BufferWebGPU*>(vertex.vertexBuffer);
		renderPassEncoder_.SetVertexBuffer(0, buffer->GetBuffer(), vertex.offset, buffer->GetSize() - vertex.offset);
	}
	if (!exclusivePass_ || indexChanged)
	{
		auto buffer = static_cast<BufferWebGPU*>(index.indexBuffer);
		const auto format = index.stride == 2 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
		renderPassEncoder_.SetIndexBuffer(buffer->GetBuffer(), format, index.offset, buffer->GetSize() - index.offset);
	}

	auto pipeline = static_cast<PipelineStateWebGPU*>(currentPipeline);
	if (!exclusivePass_ || pipelineChanged)
	{
		const auto& handle = pipeline->GetRenderPipeline();
		if (!exclusivePass_ || handle.Get() != boundPipeline_.Get())
		{
			renderPassEncoder_.SetPipeline(handle);
			if (exclusivePass_)
				boundPipeline_ = handle;
		}
		if (!exclusivePass_ || stencilReference_ != pipeline->StencilRef)
		{
			renderPassEncoder_.SetStencilReference(pipeline->StencilRef);
			stencilReference_ = pipeline->StencilRef;
		}
	}

	if (!BindResources(*pipeline, PassType::Render))
		return;
	renderPassEncoder_.DrawIndexed(primitiveCount * GetIndexCountPerPrimitive(pipeline->Topology), instanceCount);
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

	if (computePassEncoder_ == nullptr || isPipDirtied)
	{
		BeginComputePass();
		if (!computePassEncoder_)
			return;
		computePassEncoder_.SetPipeline(pip->GetComputePipeline());
	}

	if (!BindResources(*pip, PassType::Compute))
		return;

	computePassEncoder_.DispatchWorkgroups(groupX, groupY, groupZ);
	CommandList::Dispatch(groupX, groupY, groupZ, threadX, threadY, threadZ);
}

void CommandListWebGPU::SetScissor(int32_t x, int32_t y, int32_t width, int32_t height)
{
	renderPassEncoder_.SetScissorRect(x, y, width, height);
}

void CommandListWebGPU::CopyTexture(Texture* src, Texture* dst)
{
	auto srcTex = static_cast<TextureWebGPU*>(src);
	CopyTexture(src, dst, {0, 0, 0}, {0, 0, 0}, srcTex->GetParameter().Size, 0, 0);
}

void CommandListWebGPU::CopyTexture(
	Texture* src, Texture* dst, const Vec3I& srcPos, const Vec3I& dstPos, const Vec3I& size, int srcLayer, int dstLayer)
{
	if (!PrepareStandaloneCommand("CopyTexture"))
		return;

	auto srcTex = static_cast<TextureWebGPU*>(src);
	auto dstTex = static_cast<TextureWebGPU*>(dst);

	wgpu::TexelCopyTextureInfo srcTexCopy{};
	wgpu::TexelCopyTextureInfo dstTexCopy{};
	wgpu::Extent3D extend3d{};

	srcTexCopy.texture = srcTex->GetTexture();
	srcTexCopy.origin = {static_cast<uint32_t>(srcPos.X), static_cast<uint32_t>(srcPos.Y), static_cast<uint32_t>(srcLayer + srcPos.Z)};
	srcTexCopy.aspect = wgpu::TextureAspect::All;

	dstTexCopy.texture = dstTex->GetTexture();
	dstTexCopy.origin = {static_cast<uint32_t>(dstPos.X), static_cast<uint32_t>(dstPos.Y), static_cast<uint32_t>(dstLayer + dstPos.Z)};
	dstTexCopy.aspect = wgpu::TextureAspect::All;

	extend3d.width = size.X;
	extend3d.height = size.Y;
	extend3d.depthOrArrayLayers = size.Z;

	commandEncoder_.CopyTextureToTexture(&srcTexCopy, &dstTexCopy, &extend3d);
}

void CommandListWebGPU::GenerateMipMap(Texture* src)
{
	if (!PrepareStandaloneCommand("GenerateMipMap"))
		return;

	auto srcTex = static_cast<TextureWebGPU*>(src);
	if (srcTex == nullptr)
	{
		return;
	}

	srcTex->GenerateMipMaps(commandEncoder_);
}

void CommandListWebGPU::CopyBuffer(Buffer* src, Buffer* dst)
{
	if (!PrepareStandaloneCommand("CopyBuffer"))
		return;

	auto srcBuffer = static_cast<BufferWebGPU*>(src);
	auto dstBuffer = static_cast<BufferWebGPU*>(dst);
	if (srcBuffer == nullptr || dstBuffer == nullptr)
	{
		return;
	}

	srcBuffer->CopyTo(commandEncoder_, *dstBuffer);

	RegisterReferencedObject(src);
	RegisterReferencedObject(dst);
}

void CommandListWebGPU::WaitUntilCompleted()
{
	if (!device_)
	{
		return;
	}
	// A host can submit directly without calling TrackSubmission. Observe that queue at the time of the wait.
	const auto state = completion_ ? completion_ : WebGPU::ObserveQueue(device_.GetQueue());
	if (!WebGPU::WaitForCompletion(device_, state))
	{
		Log(LogType::Warning, "Timed out or failed while waiting for WebGPU command completion.");
	}
}

void CommandListWebGPU::TrackSubmission() { completion_ = WebGPU::ObserveQueue(device_.GetQueue()); }

bool CommandListWebGPU::TryGetCompleted(bool& completed) const
{
	if (!completion_)
	{
		completed = false;
		return false;
	}
	const auto value = completion_->load(std::memory_order_acquire);
	completed = value == WebGPU::CompletionStatus::Succeeded;
	return value != WebGPU::CompletionStatus::Failed;
}

} // namespace LLGI
