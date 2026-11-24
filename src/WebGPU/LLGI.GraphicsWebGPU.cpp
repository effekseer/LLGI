#include "LLGI.GraphicsWebGPU.h"

#include "LLGI.BufferWebGPU.h"
#include "LLGI.CommandListWebGPU.h"
#include "LLGI.PipelineStateWebGPU.h"
#include "LLGI.RenderPassPipelineStateWebGPU.h"
#include "LLGI.RenderPassWebGPU.h"
#include "LLGI.ShaderWebGPU.h"
#include "LLGI.TextureWebGPU.h"

#include "LLGI.CompletionWebGPU.h"
#include "LLGI.MipmapGeneratorWebGPU.h"
#include "LLGI.TextureReadbackWebGPU.h"

#include <limits>

namespace LLGI
{
class SingleFrameMemoryPoolWebGPU : public SingleFrameMemoryPool
{
	wgpu::Device device_;

	Buffer* CreateBufferInternal(int32_t size) override
	{
		auto obj = new BufferWebGPU();
		if (!obj->Initialize(device_, BufferUsageType::Constant, size))
		{
			SafeRelease(obj);
			return nullptr;
		}
		return obj;
	}

	Buffer* ReinitializeBuffer(Buffer* cb, int32_t size) override
	{
		if (cb != nullptr && cb->GetSize() >= size)
		{
			return cb;
		}
		return CreateBufferInternal(size);
	}

public:
	SingleFrameMemoryPoolWebGPU(wgpu::Device device, int32_t swapBufferCount) : SingleFrameMemoryPool(swapBufferCount), device_(device) {}
};

GraphicsWebGPU::GraphicsWebGPU(wgpu::Device device) : GraphicsWebGPU(device, nullptr) {}

GraphicsWebGPU::GraphicsWebGPU(wgpu::Device device, wgpu::Instance instance)
	: device_(device), instance_(instance), queue_(device.GetQueue()), mipmaps_(std::make_shared<WebGPU::MipmapGenerator>(device))
{
}

void GraphicsWebGPU::SetWindowSize(const Vec2I& windowSize) {}

void GraphicsWebGPU::Execute(CommandList* commandList)
{
	auto commandListWgpu = static_cast<CommandListWebGPU*>(commandList);
	auto cb = commandListWgpu->GetCommandBuffer();
	queue_.Submit(1, &cb);
	commandListWgpu->TrackSubmission();
}

void GraphicsWebGPU::WaitFinish()
{
	if (!queue_)
		return;
#ifndef __EMSCRIPTEN__
	if (instance_)
	{
		// Native instances support an unbounded wait for all submitted work.
		auto status = std::make_shared<wgpu::QueueWorkDoneStatus>();
		auto future = queue_.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
												 [status](wgpu::QueueWorkDoneStatus result, wgpu::StringView) { *status = result; });
		if (instance_.WaitAny(future, std::numeric_limits<uint64_t>::max()) != wgpu::WaitStatus::Success ||
			*status != wgpu::QueueWorkDoneStatus::Success)
			Log(LogType::Warning, "Failed while waiting for WebGPU queue completion.");
		return;
	}
#endif
	if (!WebGPU::WaitForCompletion(device_, WebGPU::ObserveQueue(queue_)))
		Log(LogType::Warning, "Timed out or failed while waiting for WebGPU queue completion.");
}

Buffer* GraphicsWebGPU::CreateBuffer(BufferUsageType usage, int32_t size)
{
	auto obj = new BufferWebGPU();
	if (!obj->Initialize(GetDevice(), usage, size, instance_))
	{
		SafeRelease(obj);
		return nullptr;
	}

	return obj;
}

Shader* GraphicsWebGPU::CreateShader(DataStructure* data, int32_t count)
{
	auto obj = new ShaderWebGPU();
	if (!obj->Initialize(GetDevice(), data, count))
	{
		SafeRelease(obj);
		return nullptr;
	}
	return obj;
}

PipelineState* GraphicsWebGPU::CreatePiplineState() { return new PipelineStateWebGPU(GetDevice()); }

SingleFrameMemoryPool* GraphicsWebGPU::CreateSingleFrameMemoryPool(int32_t constantBufferPoolSize, int32_t drawingCount)
{
	return new SingleFrameMemoryPoolWebGPU(GetDevice(), 1);
}

CommandList* GraphicsWebGPU::CreateCommandList(SingleFrameMemoryPool* memoryPool) { return new CommandListWebGPU(GetDevice()); }

RenderPass* GraphicsWebGPU::CreateRenderPass(Texture** textures, int32_t textureCount, Texture* depthTexture)
{
	auto renderPass = new RenderPassWebGPU();
	if (!renderPass->Initialize(textures, textureCount, depthTexture, nullptr, nullptr))
	{
		SafeRelease(renderPass);
	}

	return renderPass;
}

RenderPass*
GraphicsWebGPU::CreateRenderPass(Texture* texture, Texture* resolvedTexture, Texture* depthTexture, Texture* resolvedDepthTexture)
{
	if (texture == nullptr)
		return nullptr;

	auto renderPass = new RenderPassWebGPU();
	if (!renderPass->Initialize(&texture, 1, depthTexture, resolvedTexture, resolvedDepthTexture))
	{
		SafeRelease(renderPass);
	}

	return renderPass;
}

Texture* GraphicsWebGPU::CreateTexture(uint64_t id) { return nullptr; }

Texture* GraphicsWebGPU::CreateTexture(const TextureParameter& parameter)
{
	auto obj = new TextureWebGPU();
	if (!obj->Initialize(GetDevice(), parameter, instance_, mipmaps_))
	{
		SafeRelease(obj);
		return nullptr;
	}
	return obj;
}

Texture* GraphicsWebGPU::CreateTexture(const TextureInitializationParameter& parameter)
{
	TextureParameter param;
	param.Dimension = 2;
	param.Format = parameter.Format;
	param.MipLevelCount = parameter.MipMapCount;
	param.IsMipmapGenerationEnabled = parameter.MipMapCount > 1;
	param.SampleCount = 1;
	param.Size = {parameter.Size.X, parameter.Size.Y, 1};
	return CreateTexture(param);
}

Texture* GraphicsWebGPU::CreateRenderTexture(const RenderTextureInitializationParameter& parameter)
{
	TextureParameter param;
	param.Dimension = 2;
	param.Format = parameter.Format;
	param.MipLevelCount = 1;
	param.SampleCount = parameter.SamplingCount;
	param.Size = {parameter.Size.X, parameter.Size.Y, 1};
	param.Usage = TextureUsageType::RenderTarget;
	return CreateTexture(param);
}

Texture* GraphicsWebGPU::CreateDepthTexture(const DepthTextureInitializationParameter& parameter)
{
	auto format = TextureFormatType::D32;
	if (parameter.Mode == DepthTextureMode::DepthStencil)
	{
		format = TextureFormatType::D24S8;
	}

	TextureParameter param;
	param.Dimension = 2;
	param.Format = format;
	param.MipLevelCount = 1;
	param.SampleCount = parameter.SamplingCount;
	param.Size = {parameter.Size.X, parameter.Size.Y, 1};
	param.Usage = TextureUsageType::RenderTarget;
	return CreateTexture(param);
}

std::vector<uint8_t> GraphicsWebGPU::CaptureRenderTarget(Texture* renderTarget)
{
	std::vector<uint8_t> data;
	if (auto* texture = static_cast<TextureWebGPU*>(renderTarget))
	{
		const auto size = texture->GetSizeAs2D();
		if (!WebGPU::TextureReadback(device_, texture->GetTexture(), texture->GetFormat(), {size.X, size.Y, 1})
				 .Read(device_, instance_, data))
			Log(LogType::Warning, "Failed to read WebGPU render target.");
	}
	return data;
}

void GraphicsWebGPU::CaptureRenderTargetAsync(Texture* renderTarget, std::function<void(std::vector<uint8_t>)> callback)
{
	if (!callback)
		return;
	if (!renderTarget)
	{
		callback({});
		return;
	}
	auto* texture = static_cast<TextureWebGPU*>(renderTarget);
	const auto size = texture->GetSizeAs2D();
	WebGPU::TextureReadback(device_, texture->GetTexture(), texture->GetFormat(), {size.X, size.Y, 1}).ReadAsync(std::move(callback));
}

RenderPassPipelineState* GraphicsWebGPU::CreateRenderPassPipelineState(RenderPass* renderPass)
{
	return CreateRenderPassPipelineState(renderPass->GetKey());
}

RenderPassPipelineState* GraphicsWebGPU::CreateRenderPassPipelineState(const RenderPassPipelineStateKey& key)
{
	// already?
	{
		auto it = renderPassPipelineStates_.find(key);

		if (it != renderPassPipelineStates_.end())
		{
			auto ret = it->second;

			if (ret != nullptr)
			{
				auto ptr = ret.get();
				SafeAddRef(ptr);
				return ptr;
			}
		}
	}

	std::shared_ptr<RenderPassPipelineStateWebGPU> ret = LLGI::CreateSharedPtr<>(new RenderPassPipelineStateWebGPU());
	ret->SetKey(key);

	renderPassPipelineStates_[key] = ret;

	{
		auto ptr = ret.get();
		SafeAddRef(ptr);
		return ptr;
	}
}

} // namespace LLGI
