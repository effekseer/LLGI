#pragma once

#include "LLGI.BaseWebGPU.h"
#include <atomic>
#include <memory>

namespace LLGI::WebGPU
{
enum class CompletionStatus
{
	Pending,
	Succeeded,
	Failed
};

using Completion = std::shared_ptr<std::atomic<CompletionStatus>>;

Completion ObserveQueue(const wgpu::Queue& queue);
bool WaitForCompletion(const wgpu::Device& device, const Completion& completion);
bool MapReadAndWait(const wgpu::Device& device, const wgpu::Instance& instance, const wgpu::Buffer& buffer, uint64_t offset, uint64_t size);
} // namespace LLGI::WebGPU
