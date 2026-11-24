#pragma once

#include "../LLGI.Graphics.h"
#include "../LLGI.Texture.h"
#include "LLGI.BaseWebGPU.h"

#include <vector>

namespace LLGI
{

namespace WebGPU
{
class MipmapGenerator;
}

class TextureWebGPU : public Texture
{
	wgpu::Device device_;
	wgpu::Instance instance_;

	wgpu::Texture texture_;
	wgpu::TextureView textureView_;
	wgpu::TextureView baseMipView_;
	wgpu::TextureView depthOnlyView_;
	std::shared_ptr<WebGPU::MipmapGenerator> mipmaps_;
	TextureParameter parameter_;
	std::vector<uint8_t> uploadData_;

public:
	bool Initialize(wgpu::Device& device, const TextureParameter& parameter, wgpu::Instance instance = nullptr);
	bool Initialize(wgpu::Device& device,
					const TextureParameter& parameter,
					wgpu::Instance instance,
					std::shared_ptr<WebGPU::MipmapGenerator> mipmaps);
	bool InitializeFromSurfaceTexture(wgpu::Device& device, wgpu::Texture texture, const TextureParameter& parameter);
	void* Lock() override;
	void Unlock() override;
	void GenerateMipMaps(wgpu::CommandEncoder& commandEncoder);
	bool GetData(std::vector<uint8_t>& data) override;
	Vec2I GetSizeAs2D() const override;

	const TextureParameter& GetParameter() const { return parameter_; }

	wgpu::Texture GetTexture() const { return texture_; }

	const wgpu::TextureView& GetBaseMipView() const { return baseMipView_ ? baseMipView_ : textureView_; }
	const wgpu::TextureView& GetSampledView() const { return depthOnlyView_ ? depthOnlyView_ : textureView_; }
	wgpu::TextureView GetTextureView() const { return textureView_; }
};

} // namespace LLGI
