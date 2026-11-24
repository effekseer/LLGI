#pragma once

#include "LLGI.BaseWebGPU.h"

namespace LLGI::WebGPU
{
// Submits a copy of mip 0. The staging buffer and its row layout may outlive the source texture.
class TextureReadback
{
	wgpu::Buffer buffer_;
	uint32_t rowBytes_;
	uint32_t rowStride_;
	uint32_t rowCount_;

	bool CopyMappedRows(std::vector<uint8_t>& data) const;

public:
	TextureReadback(const wgpu::Device& device, const wgpu::Texture& texture, TextureFormatType format, Vec3I size);
	bool Read(const wgpu::Device& device, const wgpu::Instance& instance, std::vector<uint8_t>& data) const;
	void ReadAsync(std::function<void(std::vector<uint8_t>)> callback) const;
};
} // namespace LLGI::WebGPU
