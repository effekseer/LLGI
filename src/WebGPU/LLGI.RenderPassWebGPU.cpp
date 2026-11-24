#include "LLGI.RenderPassWebGPU.h"
#include "LLGI.TextureWebGPU.h"

namespace LLGI
{

void RenderPassWebGPU::RefreshDescriptor()
{
	const auto color = GetClearColor();
	for (size_t i = 0; i < descriptor_.colorAttachmentCount; ++i)
	{
		auto& attachment = colorAttachments_[i];
		attachment.loadOp = GetIsColorCleared() ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		attachment.clearValue = {color.R / 255.0, color.G / 255.0, color.B / 255.0, color.A / 255.0};
	}
	if (descriptor_.depthStencilAttachment)
	{
		depthStencilAttachment_.depthLoadOp = GetIsDepthCleared() ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
		depthStencilAttachment_.depthClearValue = GetClearDepth();
		if (depthStencilAttachment_.stencilStoreOp == wgpu::StoreOp::Store)
			depthStencilAttachment_.stencilLoadOp = depthStencilAttachment_.depthLoadOp;
	}
}

bool RenderPassWebGPU::Initialize(
	Texture** textures, int textureCount, Texture* depthTexture, Texture* resolvedRenderTexture, Texture* resolvedDepthTexture)
{
	if (resolvedDepthTexture != nullptr)
	{
		Log(LogType::Error, "RenderPassWebGPU : Resolved depth is not supported.");
		return false;
	}

	if (textureCount > RenderTargetMax)
	{
		Log(LogType::Error, "WebGPU render pass has too many color attachments.");
		return false;
	}
	if (!assignRenderTextures(textures, textureCount) || !assignDepthTexture(depthTexture) ||
		!assignResolvedRenderTexture(resolvedRenderTexture) || !assignResolvedDepthTexture(resolvedDepthTexture) ||
		!getSize(
			screenSize_, const_cast<const Texture**>(textures), textureCount, depthTexture, resolvedRenderTexture, resolvedDepthTexture) ||
		!sanitize())
		return false;

	descriptor_.colorAttachmentCount = textureCount;
	descriptor_.colorAttachments = colorAttachments_.data();
	for (int i = 0; i < textureCount; ++i)
	{
		auto& attachment = colorAttachments_[i];
		attachment.view = static_cast<TextureWebGPU*>(textures[i])->GetBaseMipView();
		attachment.storeOp = wgpu::StoreOp::Store;
		if (resolvedRenderTexture)
			attachment.resolveTarget = static_cast<TextureWebGPU*>(resolvedRenderTexture)->GetBaseMipView();
	}

	if (depthTexture)
	{
		depthStencilAttachment_.view = static_cast<TextureWebGPU*>(depthTexture)->GetBaseMipView();
		depthStencilAttachment_.depthStoreOp = wgpu::StoreOp::Store;
		const auto format = depthTexture->GetFormat();
		if (format == TextureFormatType::D24S8 || format == TextureFormatType::D32S8)
			depthStencilAttachment_.stencilStoreOp = wgpu::StoreOp::Store;
		descriptor_.depthStencilAttachment = &depthStencilAttachment_;
	}

	RefreshDescriptor();

	return true;
}

} // namespace LLGI
