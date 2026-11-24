#include "LLGI.BufferWebGPU.h"

#include "LLGI.CompletionWebGPU.h"

#include <algorithm>
#include <cstring>

namespace LLGI
{

namespace
{
uint64_t AlignTo(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

// Buffer-to-buffer transfers operate on words. A single row of R8 texels can
// transfer the remaining bytes without overwriting a neighbouring byte.
void CopyBufferBytes(wgpu::Device& device,
					 wgpu::CommandEncoder& encoder,
					 const wgpu::Buffer& src,
					 uint64_t srcOffset,
					 const wgpu::Buffer& dst,
					 uint64_t dstOffset,
					 uint64_t size)
{
	// Callers place source and destination at the same offset within a word.
	assert(srcOffset % 4 == dstOffset % 4);
	wgpu::TexelCopyTextureInfo scratch{};
	auto copyEdge = [&](uint64_t bytes)
	{
		if (!scratch.texture)
		{
			wgpu::TextureDescriptor desc{};
			desc.size = {3, 1, 1};
			desc.format = wgpu::TextureFormat::R8Uint;
			desc.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
			scratch.texture = device.CreateTexture(&desc);
		}
		wgpu::TexelCopyBufferInfo from{}, to{};
		from.buffer = src;
		from.layout.offset = srcOffset;
		to.buffer = dst;
		to.layout.offset = dstOffset;
		wgpu::Extent3D extent{static_cast<uint32_t>(bytes), 1, 1};
		encoder.CopyBufferToTexture(&from, &scratch, &extent);
		encoder.CopyTextureToBuffer(&scratch, &to, &extent);
		srcOffset += bytes;
		dstOffset += bytes;
		size -= bytes;
	};

	if (dstOffset % 4 != 0)
		copyEdge(std::min(size, 4 - dstOffset % 4));

	const auto words = size - size % 4;
	if (words != 0)
	{
		encoder.CopyBufferToBuffer(src, srcOffset, dst, dstOffset, words);
		srcOffset += words;
		dstOffset += words;
		size -= words;
	}
	if (size != 0)
		copyEdge(size);
}
} // namespace

bool BufferWebGPU::Initialize(wgpu::Device& device, const BufferUsageType usage, const int32_t size, wgpu::Instance instance)
{
	if (device == nullptr || size <= 0 || !VerifyUsage(usage))
	{
		return false;
	}

	device_ = device;
	instance_ = instance;

	wgpu::BufferDescriptor desc{};
	allocatedSize_ = AlignTo(size, BitwiseContains(usage, BufferUsageType::Constant) ? 16 : 4);
	desc.size = allocatedSize_;
	if (BitwiseContains(usage, BufferUsageType::MapRead))
	{
		desc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
	}
	else
	{
		desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
	}

	if ((usage & BufferUsageType::Vertex) == BufferUsageType::Vertex)
	{
		desc.usage |= wgpu::BufferUsage::Vertex;
	}

	if ((usage & BufferUsageType::Index) == BufferUsageType::Index)
	{
		desc.usage |= wgpu::BufferUsage::Index;
	}

	if ((usage & BufferUsageType::Constant) == BufferUsageType::Constant)
	{
		desc.usage |= wgpu::BufferUsage::Uniform;
	}

	if ((usage & BufferUsageType::StorageRead) == BufferUsageType::StorageRead ||
		(usage & BufferUsageType::StorageWrite) == BufferUsageType::StorageWrite)
	{
		desc.usage |= wgpu::BufferUsage::Storage;
	}

	buffer_ = device.CreateBuffer(&desc);
	size_ = size;
	usage_ = usage;
	return buffer_ != nullptr;
}

void* BufferWebGPU::Lock() { return Lock(0, GetSize()); }

void* BufferWebGPU::Lock(int32_t offset, int32_t size)
{
	if (offset < 0 || size <= 0 || offset > size_ - size)
		return nullptr;
	lockedOffset_ = offset;

	if (BitwiseContains(usage_, BufferUsageType::MapRead))
	{
		const uint64_t mapOffset = offset - offset % 8;
		const auto mapSize = AlignTo(static_cast<uint64_t>(offset) + size, 4) - mapOffset;
		if (!WebGPU::MapReadAndWait(device_, instance_, buffer_, mapOffset, mapSize))
			return nullptr;
		auto* mapped = static_cast<const uint8_t*>(buffer_.GetConstMappedRange(mapOffset, mapSize));
		return mapped ? const_cast<uint8_t*>(mapped + (offset - mapOffset)) : nullptr;
	}

	// Padding after the logical end is ours to write. Bytes inside the logical range belong to the caller.
	lockedBuffer_.resize(offset + size == size_ && offset % 4 == 0 ? AlignTo(size, 4) : size);
	return lockedBuffer_.data();
}

void BufferWebGPU::Unlock()
{
	if (lockedBuffer_.empty())
	{
		if (BitwiseContains(usage_, BufferUsageType::MapRead))
		{
			buffer_.Unmap();
		}
		return;
	}

	if (lockedOffset_ % 4 == 0 && lockedBuffer_.size() % 4 == 0)
	{
		device_.GetQueue().WriteBuffer(buffer_, lockedOffset_, lockedBuffer_.data(), lockedBuffer_.size());
	}
	else
	{
		const uint32_t prefix = lockedOffset_ % 4;
		wgpu::BufferDescriptor desc{};
		desc.size = AlignTo(static_cast<uint64_t>(prefix) + lockedBuffer_.size(), 4);
		desc.usage = wgpu::BufferUsage::CopySrc;
		desc.mappedAtCreation = true;
		auto upload = device_.CreateBuffer(&desc);
		std::memcpy(static_cast<uint8_t*>(upload.GetMappedRange()) + prefix, lockedBuffer_.data(), lockedBuffer_.size());
		upload.Unmap();

		auto encoder = device_.CreateCommandEncoder();
		CopyBufferBytes(device_, encoder, upload, prefix, buffer_, lockedOffset_, lockedBuffer_.size());
		auto commands = encoder.Finish();
		device_.GetQueue().Submit(1, &commands);
	}
	lockedBuffer_.clear();
	lockedOffset_ = 0;
}

void BufferWebGPU::CopyTo(wgpu::CommandEncoder& encoder, BufferWebGPU& destination)
{
	const auto size = std::min(size_, destination.size_);
	// The final word can include padding only when the copy reaches the end of the destination.
	const uint64_t bytes = size == destination.size_ ? AlignTo(size, 4) : size;
	CopyBufferBytes(device_, encoder, buffer_, 0, destination.buffer_, 0, bytes);
}

int32_t BufferWebGPU::GetSize() { return size_; }

wgpu::Buffer& BufferWebGPU::GetBuffer() { return buffer_; }

} // namespace LLGI
