#include "LLGI.CommandListWebGPU.h"
#include "LLGI.BufferWebGPU.h"
#include "LLGI.PipelineStateWebGPU.h"
#include "LLGI.RenderPassWebGPU.h"
#include "LLGI.TextureWebGPU.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace LLGI
{
namespace
{
constexpr uint32_t TextureBytesPerRowAlignment = 256;
constexpr std::array<wgpu::FilterMode, 2> TextureFilterModes = {
	wgpu::FilterMode::Nearest,
	wgpu::FilterMode::Linear,
};
constexpr std::array<wgpu::AddressMode, 3> TextureAddressModes = {
	wgpu::AddressMode::ClampToEdge,
	wgpu::AddressMode::Repeat,
	wgpu::AddressMode::MirrorRepeat,
};

ShaderResourceType GetTextureBindingResourceType(TextureWebGPU* texture)
{
	if (texture != nullptr && BitwiseContains(texture->GetParameter().Usage, TextureUsageType::Storage))
	{
		return ShaderResourceType::StorageTexture;
	}
	return ShaderResourceType::Texture;
}

bool NeedsTextureSampler(TextureWebGPU* texture)
{
	return texture == nullptr || !BitwiseContains(texture->GetParameter().Usage, TextureUsageType::Storage);
}
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
	if (isInRenderPass_ && !doesBeginWithPlatform_)
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
	fallbackTexture_ = device_.CreateTexture(&fallbackTextureDesc);
	fallbackTextureView_ = fallbackTexture_.CreateView();

	std::array<uint8_t, TextureBytesPerRowAlignment> fallbackTextureData{};
	fallbackTextureData[0] = 255;
	fallbackTextureData[1] = 255;
	fallbackTextureData[2] = 255;
	fallbackTextureData[3] = 255;
	wgpu::TexelCopyTextureInfo fallbackDst{};
	fallbackDst.texture = fallbackTexture_;
	fallbackDst.aspect = wgpu::TextureAspect::All;
	wgpu::TexelCopyBufferLayout fallbackLayout{};
	fallbackLayout.bytesPerRow = TextureBytesPerRowAlignment;
	fallbackLayout.rowsPerImage = 1;
	wgpu::Extent3D fallbackExtent{};
	fallbackExtent.width = 1;
	fallbackExtent.height = 1;
	fallbackExtent.depthOrArrayLayers = 1;
	device_.GetQueue().WriteTexture(&fallbackDst, fallbackTextureData.data(), fallbackTextureData.size(), &fallbackLayout, &fallbackExtent);

	for (int w = 0; w < 3; w++)
	{
		for (int f = 0; f < 2; f++)
		{
			wgpu::SamplerDescriptor samplerDesc{};

			samplerDesc.magFilter = TextureFilterModes[f];
			samplerDesc.minFilter = TextureFilterModes[f];
			samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
			samplerDesc.lodMinClamp = 0.0f;
			samplerDesc.lodMaxClamp = 32.0f;
			samplerDesc.maxAnisotropy = 1;
			samplerDesc.addressModeU = TextureAddressModes[w];
			samplerDesc.addressModeV = TextureAddressModes[w];
			samplerDesc.addressModeW = TextureAddressModes[w];
			samplers_[w][f] = device.CreateSampler(&samplerDesc);
		}
	}
}

void CommandListWebGPU::Begin()
{
	completion_.reset();
	wgpu::CommandEncoderDescriptor desc = {};
	commandEncorder_ = device_.CreateCommandEncoder(&desc);

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
	commandEncorder_ = wgpu::CommandEncoder::Acquire(reinterpret_cast<WGPUCommandEncoder>(platformContextPtr));

	return CommandList::BeginWithPlatform(platformContextPtr);
}

void CommandListWebGPU::End()
{
	FinishRecording();
	commandBuffer_ = commandEncorder_.Finish();
	commandEncorder_ = nullptr;

	CommandList::End();
}

void CommandListWebGPU::EndWithPlatform()
{
	FinishRecording();
	commandEncorder_ = nullptr;
	commandBuffer_ = nullptr;

	CommandList::EndWithPlatform();
}

bool CommandListWebGPU::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
	if (!ValidateViewport(x, y, width, height, minDepth, maxDepth)) return false;
	if (renderPassEncorder_ == nullptr) return false;
	renderPassEncorder_.SetViewport(x, y, width, height, minDepth, maxDepth);
	return true;
}

void CommandListWebGPU::BeginRenderPass(RenderPass* renderPass)
{
	EndComputePass();

	auto rp = static_cast<RenderPassWebGPU*>(renderPass);
	rp->RefreshDescriptor();
	const auto& desc = rp->GetDescriptor();

	renderPassEncorder_ = commandEncorder_.BeginRenderPass(&desc);
	renderPassEncorder_.SetViewport(
		0.0f, 0.0f, static_cast<float>(rp->GetScreenSize().X), static_cast<float>(rp->GetScreenSize().Y), 0.0f, 1.0f);

	ResetRenderState(true);
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

bool CommandListWebGPU::BeginRenderPassWithPlatformPtr(void* platformPtr) { return BeginRenderPassWithPlatformPtr(platformPtr, false); }

bool CommandListWebGPU::BeginRenderPassWithPlatformPtr(void* platformPtr, bool exclusive)
{
	if (platformPtr == nullptr)
	{
		return false;
	}

	EndComputePass();
	renderPassEncorder_ = wgpu::RenderPassEncoder::Acquire(reinterpret_cast<WGPURenderPassEncoder>(platformPtr));

	ResetRenderState(exclusive);
	return CommandList::BeginRenderPassWithPlatformPtr(platformPtr);
}

bool CommandListWebGPU::EndRenderPassWithPlatformPtr()
{
	if (renderPassEncorder_ != nullptr)
	{
		renderPassEncorder_ = nullptr;
	}

	CommandList::EndRenderPass();
	return true;
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

void CommandListWebGPU::BindResources(PipelineStateWebGPU& pipeline, PassType pass)
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
		entry.size = cb->GetAllocatedSize() - cb->GetOffset();
		entry.offset = cb->GetOffset();
		bindGroupEntries_[0].push_back(entry);
	}

	for (int unit_ind = 0; unit_ind < static_cast<int32_t>(currentTextures_.size()); unit_ind++)
	{
		// For automatic layouts only the caller can identify a texture slot (including an explicitly bound null texture).
		if (!pipeline.HasBindingReflection() && !currentTextures_[unit_ind].isBound)
			continue;
		auto texture = static_cast<TextureWebGPU*>(currentTextures_[unit_ind].texture);
		const auto resourceType = GetTextureBindingResourceType(texture);
		if (!pipeline.HasBinding(1, static_cast<uint32_t>(unit_ind), resourceType))
			continue;
		auto wm = (int32_t)currentTextures_[unit_ind].wrapMode;
		auto mm = (int32_t)currentTextures_[unit_ind].minMagFilter;

		wgpu::BindGroupEntry textureEntry = {};
		textureEntry.binding = unit_ind;
		textureEntry.textureView = texture != nullptr ? texture->GetTextureView() : fallbackTextureView_;
		bindGroupEntries_[1].push_back(textureEntry);

		wgpu::BindGroupEntry samplerEntry = {};
		if (NeedsTextureSampler(texture))
		{
			if (!pipeline.HasBinding(2, static_cast<uint32_t>(unit_ind), ShaderResourceType::Sampler))
			{
				continue;
			}
			samplerEntry.binding = unit_ind;
			samplerEntry.sampler = samplers_[wm][mm];
			bindGroupEntries_[2].push_back(samplerEntry);
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
		bufferEntry.offset = buffer->GetOffset();
		bufferEntry.size = buffer->GetSize();
		bindGroupEntries_[storageGroup].push_back(bufferEntry);
	}

	for (uint32_t slot = 0; slot < bindGroupEntries_.size(); ++slot)
	{
		const auto& entries = bindGroupEntries_[slot];
		if (entries.empty())
		{
			continue;
		}
		wgpu::BindGroupDescriptor desc{};
		desc.layout = pipeline.GetBindGroupLayout(slot);
		desc.entries = entries.data();
		desc.entryCount = entries.size();
		auto group = GetBindGroup(desc);
		if (pass == PassType::Compute)
		{
			computePassEncorder_.SetBindGroup(slot, group);
		}
		else if (!exclusivePass_ || boundGroups_[slot].Get() != group.Get())
		{
			renderPassEncorder_.SetBindGroup(slot, group);
			if (exclusivePass_)
			{
				boundGroups_[slot] = std::move(group);
			}
		}
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

	if (vb != nullptr && isVBDirtied)
	{
		renderPassEncorder_.SetVertexBuffer(0, vb->GetBuffer(), bvb.offset, bvb.vertexBuffer->GetSize() - bvb.offset);
	}

	if (ib != nullptr && isIBDirtied)
	{
		const auto format = bib.stride == 2 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
		renderPassEncorder_.SetIndexBuffer(ib->GetBuffer(), format, bib.offset, ib->GetSize() - bib.offset);
	}

	if (pip != nullptr && isPipDirtied)
	{
		const auto& pipeline = pip->GetRenderPipeline();
		if (!exclusivePass_ || pipeline.Get() != boundPipeline_.Get())
		{
			renderPassEncorder_.SetPipeline(pipeline);
			if (exclusivePass_)
				boundPipeline_ = pipeline;
		}
		if (!exclusivePass_ || stencilReference_ != pip->StencilRef)
		{
			renderPassEncorder_.SetStencilReference(pip->StencilRef);
			stencilReference_ = pip->StencilRef;
		}
	}

	BindResources(*pip, PassType::Render);

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

	bool isComputePassStarted = false;
	if (computePassEncorder_ == nullptr)
	{
		BeginComputePass();
		isComputePassStarted = true;
	}

	if (isComputePassStarted || isPipDirtied)
	{
		computePassEncorder_.SetPipeline(pip->GetComputePipeline());
	}

	BindResources(*pip, PassType::Compute);

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

	commandEncorder_.CopyTextureToTexture(&srcTexCopy, &dstTexCopy, &extend3d);
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

	srcTex->GenerateMipMaps(commandEncorder_);
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

	commandEncorder_.CopyBufferToBuffer(
		srcBuffer->GetBuffer(), 0, dstBuffer->GetBuffer(), 0, std::min(srcBuffer->GetSize(), dstBuffer->GetSize()));

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
	const auto state = completion_ ? completion_ : ObserveQueueCompletion();

#if defined(__EMSCRIPTEN__)
	const double waitStart = emscripten_get_now();
	while (state->load(std::memory_order_acquire) == SubmissionStatus::Pending)
	{
		emscripten_sleep(1);
		if (emscripten_get_now() - waitStart > 5000.0)
		{
			break;
		}
	}
#else
	const auto waitStart = std::chrono::steady_clock::now();
	while (state->load(std::memory_order_acquire) == SubmissionStatus::Pending)
	{
		device_.Tick();
		if (std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(5))
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
#endif

	if (state->load(std::memory_order_acquire) != SubmissionStatus::Completed)
	{
		Log(LogType::Warning, "Timed out or failed while waiting for WebGPU command completion.");
	}
}

CommandListWebGPU::Completion CommandListWebGPU::ObserveQueueCompletion() const
{
	auto state = std::make_shared<std::atomic<SubmissionStatus>>(SubmissionStatus::Pending);
	device_.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
										   [state](wgpu::QueueWorkDoneStatus status, wgpu::StringView)
										   {
											   state->store(status == wgpu::QueueWorkDoneStatus::Success ? SubmissionStatus::Completed
																										 : SubmissionStatus::Failed,
															std::memory_order_release);
										   });
	return state;
}

void CommandListWebGPU::TrackSubmission() { completion_ = ObserveQueueCompletion(); }

bool CommandListWebGPU::TryGetCompleted(bool& completed) const
{
	if (!completion_)
	{
		completed = false;
		return false;
	}
	const auto value = completion_->load(std::memory_order_acquire);
	completed = value == SubmissionStatus::Completed;
	return value != SubmissionStatus::Failed;
}

} // namespace LLGI
