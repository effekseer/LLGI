#include "LLGI.CompletionWebGPU.h"

#include <chrono>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <thread>
#endif

namespace LLGI::WebGPU
{
namespace
{
constexpr auto WaitTimeout = std::chrono::seconds(5);
}

Completion ObserveQueue(const wgpu::Queue& queue)
{
	auto completion = std::make_shared<std::atomic<CompletionStatus>>(CompletionStatus::Pending);
	queue.OnSubmittedWorkDone(
		wgpu::CallbackMode::AllowSpontaneous,
		[completion](wgpu::QueueWorkDoneStatus status, wgpu::StringView)
		{ completion->store(status == wgpu::QueueWorkDoneStatus::Success ? CompletionStatus::Succeeded : CompletionStatus::Failed); });
	return completion;
}

bool WaitForCompletion(const wgpu::Device& device, const Completion& completion)
{
	const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
	while (completion->load() == CompletionStatus::Pending && std::chrono::steady_clock::now() < deadline)
	{
#ifdef __EMSCRIPTEN__
		emscripten_sleep(1);
#else
		device.Tick();
		if (completion->load() == CompletionStatus::Pending)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
	}
	return completion->load() == CompletionStatus::Succeeded;
}

bool MapReadAndWait(const wgpu::Device& device, const wgpu::Instance& instance, const wgpu::Buffer& buffer, uint64_t offset, uint64_t size)
{
#ifdef __EMSCRIPTEN__
	const auto mode = wgpu::CallbackMode::AllowSpontaneous;
#else
	// A device supplied without an instance has no event loop to dispatch AllowProcessEvents callbacks.
	const auto mode = instance ? wgpu::CallbackMode::WaitAnyOnly : wgpu::CallbackMode::AllowSpontaneous;
#endif
	// A callback can arrive after the synchronous caller has timed out.
	auto completion = std::make_shared<std::atomic<CompletionStatus>>(CompletionStatus::Pending);
	auto future = buffer.MapAsync(
		wgpu::MapMode::Read,
		offset,
		size,
		mode,
		[completion](wgpu::MapAsyncStatus status, wgpu::StringView)
		{ completion->store(status == wgpu::MapAsyncStatus::Success ? CompletionStatus::Succeeded : CompletionStatus::Failed); });
	if (instance)
	{
		instance.WaitAny(future, std::chrono::duration_cast<std::chrono::nanoseconds>(WaitTimeout).count());
		return completion->load() == CompletionStatus::Succeeded;
	}
	return WaitForCompletion(device, completion);
}
} // namespace LLGI::WebGPU
