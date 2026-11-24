#include "LLGI.TextureReadbackWebGPU.h"
#include "LLGI.CompletionWebGPU.h"

#include <cstring>

namespace LLGI::WebGPU
{
TextureReadback::TextureReadback(const wgpu::Device& device, const wgpu::Texture& texture, TextureFormatType format, Vec3I size)
	: rowBytes_(GetTextureRowPitch(format, size)), rowStride_((rowBytes_ + 255) / 256 * 256), rowCount_(GetTextureRowCount(format, size))
{
	auto encoder = device.CreateCommandEncoder();
	wgpu::TexelCopyTextureInfo src{};
	src.texture = texture;
	if (texture.GetSampleCount() > 1)
	{
		if (IsDepthFormat(format) || format == TextureFormatType::R32_FLOAT || format == TextureFormatType::R32G32_FLOAT ||
			format == TextureFormatType::R32G32B32A32_FLOAT ||
			(texture.GetUsage() & wgpu::TextureUsage::RenderAttachment) == wgpu::TextureUsage::None)
		{
			Log(LogType::Warning, "WebGPU readback requires a resolvable multisampled color attachment.");
			return;
		}
		wgpu::TextureDescriptor resolved{};
		resolved.size = {static_cast<uint32_t>(size.X), static_cast<uint32_t>(size.Y), 1};
		resolved.format = texture.GetFormat();
		resolved.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
		src.texture = device.CreateTexture(&resolved);

		wgpu::RenderPassColorAttachment color{};
		color.view = texture.CreateView();
		color.resolveTarget = src.texture.CreateView();
		color.loadOp = wgpu::LoadOp::Load;
		color.storeOp = wgpu::StoreOp::Store;
		wgpu::RenderPassDescriptor pass{};
		pass.colorAttachmentCount = 1;
		pass.colorAttachments = &color;
		encoder.BeginRenderPass(&pass).End();
	}
	else if ((texture.GetUsage() & wgpu::TextureUsage::CopySrc) == wgpu::TextureUsage::None || format == TextureFormatType::D24S8 ||
			 format == TextureFormatType::D32S8)
	{
		Log(LogType::Warning, "WebGPU readback requires a copyable texture with a single aspect.");
		return;
	}

	wgpu::BufferDescriptor desc{};
	desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
	desc.size = static_cast<uint64_t>(rowStride_) * rowCount_;
	buffer_ = device.CreateBuffer(&desc);

	wgpu::TexelCopyBufferInfo dst{};
	dst.buffer = buffer_;
	dst.layout.bytesPerRow = rowStride_;
	dst.layout.rowsPerImage = GetTextureRowCount(format, {size.X, size.Y, 1});
	auto extent = GetTextureCopyExtent(format, size);
	encoder.CopyTextureToBuffer(&src, &dst, &extent);
	auto commands = encoder.Finish();
	device.GetQueue().Submit(1, &commands);
}

bool TextureReadback::CopyMappedRows(std::vector<uint8_t>& data) const
{
	auto mapped = static_cast<const uint8_t*>(buffer_.GetConstMappedRange());
	if (mapped)
	{
		data.resize(static_cast<size_t>(rowBytes_) * rowCount_);
		for (size_t row = 0; row < rowCount_; ++row)
			std::memcpy(data.data() + row * rowBytes_, mapped + row * rowStride_, rowBytes_);
	}
	buffer_.Unmap();
	return mapped != nullptr;
}

bool TextureReadback::Read(const wgpu::Device& device, const wgpu::Instance& instance, std::vector<uint8_t>& data) const
{
	data.clear();
	return buffer_ && MapReadAndWait(device, instance, buffer_, 0, buffer_.GetSize()) && CopyMappedRows(data);
}

void TextureReadback::ReadAsync(std::function<void(std::vector<uint8_t>)> callback) const
{
	if (!buffer_)
	{
		callback({});
		return;
	}
	buffer_.MapAsync(wgpu::MapMode::Read,
					 0,
					 buffer_.GetSize(),
					 wgpu::CallbackMode::AllowSpontaneous,
					 [readback = *this, callback = std::move(callback)](wgpu::MapAsyncStatus status, wgpu::StringView message)
					 {
						 std::vector<uint8_t> data;
						 if (status != wgpu::MapAsyncStatus::Success)
							 Log(LogType::Warning, "WebGPU texture readback failed: " + std::string(std::string_view(message)));
						 else if (!readback.CopyMappedRows(data))
							 Log(LogType::Warning, "WebGPU texture readback returned no mapped range.");
						 callback(std::move(data));
					 });
}
} // namespace LLGI::WebGPU
