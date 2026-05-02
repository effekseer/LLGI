#pragma once

#include "../LLGI.Graphics.h"
#include "../LLGI.Texture.h"
#include "LLGI.BaseWebGPU.h"

namespace LLGI
{

class TextureWebGPU : public Texture
{
	wgpu::Device device_;
	wgpu::Instance instance_;

	wgpu::Texture texture_;
	wgpu::TextureView textureView_;
	TextureParameter parameter_;
	std::vector<uint8_t> temp_buffer_;

public:
	bool Initialize(wgpu::Device& device, const TextureParameter& parameter, wgpu::Instance instance = nullptr);
	bool InitializeFromSurfaceTexture(wgpu::Device& device, wgpu::Texture texture, const TextureParameter& parameter);
	void* Lock() override;
	void Unlock() override;
	bool GetData(std::vector<uint8_t>& data) override;
	Vec2I GetSizeAs2D() const override;
	bool IsRenderTexture() const override;
	bool IsDepthTexture() const override;

	const TextureParameter& GetParameter() const { return parameter_; }

	wgpu::Texture GetTexture() const { return texture_; }

	wgpu::TextureView GetTextureView() const { return textureView_; }
};

} // namespace LLGI
