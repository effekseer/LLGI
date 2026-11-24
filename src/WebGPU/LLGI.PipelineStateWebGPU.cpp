#include "LLGI.PipelineStateWebGPU.h"
#include "LLGI.CompletionWebGPU.h"
#include "LLGI.RenderPassPipelineStateWebGPU.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <array>
#include <limits>

namespace LLGI
{
PipelineStateWebGPU::PipelineStateWebGPU(wgpu::Device device) : device_(device) { shaders_.fill(nullptr); }

PipelineStateWebGPU::~PipelineStateWebGPU()
{
	for (auto& shader : shaders_)
	{
		SafeRelease(shader);
	}
}

void PipelineStateWebGPU::SetShader(ShaderStageType stage, Shader* shader)
{
	SafeAddRef(shader);
	SafeRelease(shaders_[static_cast<int>(stage)]);
	shaders_[static_cast<int>(stage)] = shader;
}

bool PipelineStateWebGPU::HasBinding(uint32_t group, uint32_t binding) const
{
	if (!hasBindingReflection_)
	{
		return true;
	}

	for (const auto& b : bindings_)
	{
		if (b.Group == group && b.Binding == binding)
		{
			return true;
		}
	}

	return false;
}

bool PipelineStateWebGPU::HasBinding(uint32_t group, uint32_t binding, ShaderResourceType resourceType) const
{
	return HasBinding(group, binding, resourceType, ShaderResourceAccess::ReadOnly);
}

bool PipelineStateWebGPU::HasBinding(uint32_t group, uint32_t binding, ShaderResourceType resourceType, ShaderResourceAccess access) const
{
	if (!hasBindingReflection_ && resourceType != ShaderResourceType::Texture && resourceType != ShaderResourceType::StorageTexture &&
		resourceType != ShaderResourceType::Sampler)
		return true;

	for (const auto& b : bindings_)
	{
		const bool resourceTypeMatches = b.Resource.ResourceType == resourceType || b.Resource.ResourceType == ShaderResourceType::Unknown;
		const bool accessMatches = b.Resource.ResourceType == ShaderResourceType::Unknown ||
								   resourceType != ShaderResourceType::StorageBuffer ||
								   IsReadOnly(b.Resource.Access) == IsReadOnly(access);
		if (b.Group == group && b.Binding == binding && resourceTypeMatches && accessMatches)
		{
			return true;
		}
	}

	return false;
}

bool PipelineStateWebGPU::CompileAsync(std::function<void(bool)> onValidated)
{
	device_.PushErrorScope(wgpu::ErrorFilter::Validation);
	const bool created = CreatePipeline();
	device_.PopErrorScope(
		wgpu::CallbackMode::AllowSpontaneous,
		[created, onValidated = std::move(onValidated)](wgpu::PopErrorScopeStatus status, wgpu::ErrorType error, wgpu::StringView message)
		{
			const bool valid = status == wgpu::PopErrorScopeStatus::Success && error == wgpu::ErrorType::NoError;
			if (!valid)
				Log(LogType::Error, "WebGPU pipeline validation failed: " + std::string(std::string_view(message)));
			if (onValidated)
				onValidated(created && valid);
		});
	return created;
}

bool PipelineStateWebGPU::Compile()
{
#ifdef __EMSCRIPTEN__
	if (!emscripten_has_asyncify())
		return CompileAsync({});
#endif
	auto result = std::make_shared<std::atomic<WebGPU::CompletionStatus>>(WebGPU::CompletionStatus::Pending);
	CompileAsync([result](bool valid) { result->store(valid ? WebGPU::CompletionStatus::Succeeded : WebGPU::CompletionStatus::Failed); });
	if (WebGPU::WaitForCompletion(device_, result))
		return true;
	renderPipeline_ = nullptr;
	computePipeline_ = nullptr;
	return false;
}

wgpu::BindGroupLayout PipelineStateWebGPU::GetBindGroupLayout(uint32_t index) const
{
	if (index < bindGroupLayouts_.size() && bindGroupLayouts_[index])
		return bindGroupLayouts_[index];
	return computePipeline_ ? computePipeline_.GetBindGroupLayout(index) : renderPipeline_.GetBindGroupLayout(index);
}

bool PipelineStateWebGPU::CacheBindGroupLayouts()
{
	if (!computePipeline_ && !renderPipeline_)
		return false;
	if (hasBindingReflection_)
	{
		for (uint32_t i = 0; i < bindingCounts_.size(); ++i)
			if (bindingCounts_[i])
				bindGroupLayouts_[i] = GetBindGroupLayout(i);
	}
	return true;
}

bool PipelineStateWebGPU::CreatePipeline()
{
	auto vertexShader = static_cast<ShaderWebGPU*>(shaders_[static_cast<int>(ShaderStageType::Vertex)]);
	auto pixelShader = static_cast<ShaderWebGPU*>(shaders_[static_cast<int>(ShaderStageType::Pixel)]);
	auto computeShader = static_cast<ShaderWebGPU*>(shaders_[static_cast<int>(ShaderStageType::Compute)]);
	renderPipeline_ = nullptr;
	computePipeline_ = nullptr;
	bindingCounts_ = {};
	comparisonSamplers_ = {};
	whiteFallbackSlots_ = {};
	bindGroupLayouts_ = {};
	bindings_.clear();
	hasBindingReflection_ = true;
	if (!computeShader && (!vertexShader || !pixelShader || !renderPassPipelineState_ || !ValidateVertexLayout()))
		return false;
	for (size_t stage = 0; stage < shaders_.size(); ++stage)
	{
		auto shader = shaders_[stage];
		if (!shader || (computeShader && stage != static_cast<size_t>(ShaderStageType::Compute)))
			continue;
		const auto* shaderWebGPU = static_cast<ShaderWebGPU*>(shader);
		if (!shaderWebGPU->HasBindingReflection())
		{
			// SPIR-V supplies partial binding metadata.
			hasBindingReflection_ = false;
		}
		for (const auto& reflectedBinding : shaderWebGPU->GetBindings())
		{
			auto existing = std::find(bindings_.begin(), bindings_.end(), reflectedBinding);
			if (existing != bindings_.end())
				continue;
			if (reflectedBinding.Group >= bindingCounts_.size())
				return false;
			bindings_.push_back(reflectedBinding);
			++bindingCounts_[reflectedBinding.Group];
			if (reflectedBinding.Group == 1 && reflectedBinding.SupportsWhiteFallback &&
				reflectedBinding.Binding < whiteFallbackSlots_.size())
				whiteFallbackSlots_[reflectedBinding.Binding] = true;
			if (reflectedBinding.ComparisonSampler && reflectedBinding.Binding < comparisonSamplers_.size())
				comparisonSamplers_[reflectedBinding.Binding] = true;
		}
	}

	const char* entryPointName = "main";
	if (computeShader != nullptr)
	{
		wgpu::ComputePipelineDescriptor desc{};
		desc.compute.module = computeShader->GetShaderModule();
		desc.compute.entryPoint = entryPointName;
		computePipeline_ = device_.CreateComputePipeline(&desc);
		return CacheBindGroupLayouts();
	}

	wgpu::RenderPipelineDescriptor desc{};

	desc.primitive.topology = Convert(Topology);
	desc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined; // is it correct?
	desc.primitive.frontFace = wgpu::FrontFace::CW;
	desc.primitive.cullMode = Convert(Culling);
	desc.multisample.count = static_cast<uint32_t>(renderPassPipelineState_->Key.SamplingCount);
	desc.multisample.mask = std::numeric_limits<int32_t>::max();
	desc.multisample.alphaToCoverageEnabled = false;

	desc.vertex.module = vertexShader->GetShaderModule();
	desc.vertex.entryPoint = entryPointName;

	desc.vertex.bufferCount = 1;
	std::array<wgpu::VertexBufferLayout, 1> bufferLayouts{};
	desc.vertex.buffers = bufferLayouts.data();

	bufferLayouts[0].attributeCount = VertexLayoutCount;
	bufferLayouts[0].arrayStride = 0;
	bufferLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;

	std::array<wgpu::VertexAttribute, VertexLayoutMax> attributes{};
	bufferLayouts[0].attributes = attributes.data();

	int offset = 0;
	int vertexExtent = 0;
	for (int i = 0; i < VertexLayoutCount; i++)
	{
		if (VertexLayoutOffsets[i] >= 0) offset = VertexLayoutOffsets[i];
		attributes[i].format = Convert(VertexLayouts[i]);
		attributes[i].offset = offset;
		attributes[i].shaderLocation = VertexLayoutLocations[i] >= 0 ? VertexLayoutLocations[i] : i;
		offset += GetSize(VertexLayouts[i]);
		vertexExtent = std::max(vertexExtent, offset);
	}
	bufferLayouts[0].arrayStride = VertexBufferStride > 0 ? VertexBufferStride : vertexExtent;

	auto blendComponent = [](BlendEquationType equation, BlendFuncType source, BlendFuncType destination)
	{
		wgpu::BlendComponent component{};
		component.operation = Convert(equation);
		const bool minMax = equation == BlendEquationType::Min || equation == BlendEquationType::Max;
		component.srcFactor = minMax ? wgpu::BlendFactor::One : Convert(source);
		component.dstFactor = minMax ? wgpu::BlendFactor::One : Convert(destination);
		return component;
	};
	wgpu::BlendState blendState{};
	blendState.color = blendComponent(BlendEquationRGB, BlendSrcFunc, BlendDstFunc);
	blendState.alpha = blendComponent(BlendEquationAlpha, BlendSrcFuncAlpha, BlendDstFuncAlpha);

	std::array<wgpu::ColorTargetState, RenderTargetMax> colorTargetStates{};

	for (size_t i = 0; i < renderPassPipelineState_->Key.RenderTargetFormats.size(); i++)
	{
		colorTargetStates[i].blend = IsBlendEnabled ? &blendState : nullptr;
		colorTargetStates[i].format = ConvertFormat(renderPassPipelineState_->Key.RenderTargetFormats.at(i));
		colorTargetStates[i].writeMask = wgpu::ColorWriteMask::All;
	}

	wgpu::FragmentState fragmentState = {};
	fragmentState.targetCount = static_cast<uint32_t>(renderPassPipelineState_->Key.RenderTargetFormats.size());
	fragmentState.targets = colorTargetStates.data();
	fragmentState.entryPoint = entryPointName;
	fragmentState.module = pixelShader->GetShaderModule();

	desc.fragment = &fragmentState;

	const bool hasDepth = renderPassPipelineState_->Key.DepthFormat != TextureFormatType::Unknown;
	const bool hasStencil = HasStencil(renderPassPipelineState_->Key.DepthFormat);

	wgpu::DepthStencilState depthStencilState = {};
	depthStencilState.depthWriteEnabled = hasDepth && IsDepthWriteEnabled;

	if (IsDepthTestEnabled)
	{
		depthStencilState.depthCompare = Convert(DepthFunc);
	}
	else
	{
		depthStencilState.depthCompare = wgpu::CompareFunction::Always;
	}

	if (hasStencil && IsStencilTestEnabled)
	{
		wgpu::StencilFaceState fs{};

		fs.compare = Convert(StencilCompareFunc);
		fs.depthFailOp = Convert(StencilDepthFailOp);
		fs.failOp = Convert(StencilFailOp);
		fs.passOp = Convert(StencilPassOp);

		depthStencilState.stencilFront = fs;
		depthStencilState.stencilBack = fs;

		depthStencilState.stencilWriteMask = StencilWriteMask;
		depthStencilState.stencilReadMask = StencilReadMask;
	}
	else
	{
		wgpu::StencilFaceState fs{};

		fs.depthFailOp = wgpu::StencilOperation::Keep;
		fs.failOp = wgpu::StencilOperation::Keep;
		fs.compare = wgpu::CompareFunction::Always;
		fs.passOp = wgpu::StencilOperation::Keep;

		depthStencilState.stencilFront = fs;
		depthStencilState.stencilBack = fs;

		depthStencilState.stencilWriteMask = 0xff;
		depthStencilState.stencilReadMask = 0xff;
	}

	if (hasDepth)
	{
		depthStencilState.format = ConvertFormat(renderPassPipelineState_->Key.DepthFormat);
		desc.depthStencil = &depthStencilState;
	}

	renderPipeline_ = device_.CreateRenderPipeline(&desc);

	return CacheBindGroupLayouts();
}

} // namespace LLGI
