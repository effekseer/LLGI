#include "LLGI.ShaderWebGPU.h"
#include <cstring>
#include <string>

namespace LLGI
{
ShaderWebGPU::ShaderWebGPU() {}

ShaderWebGPU::~ShaderWebGPU() {}

bool ShaderWebGPU::Initialize(wgpu::Device& device, DataStructure* data, int32_t count)
{
	static const char wgslHeader[] = {'w', 'g', 's', 'l', 'c', 'o', 'd', 'e'};

	if (data == nullptr || count == 0)
	{
		return false;
	}

	wgpu::ShaderModuleDescriptor desc = {};

	if (data[0].Data == nullptr || data[0].Size <= 0)
	{
		return false;
	}

	const auto* bytes = static_cast<const uint8_t*>(data[0].Data);
	const bool hasWGSLHeader = data[0].Size >= static_cast<int32_t>(sizeof(wgslHeader)) &&
							   memcmp(bytes, wgslHeader, sizeof(wgslHeader)) == 0;
	const bool hasSPIRVMagic = data[0].Size >= 4 && bytes[0] == 0x03 && bytes[1] == 0x02 && bytes[2] == 0x23 && bytes[3] == 0x07;

	wgpu::ShaderSourceSPIRV sprivDesc = {};
	wgpu::ShaderSourceWGSL wgslDesc = {};
	std::string wgslCode;

	if (!hasSPIRVMagic)
	{
		const auto codeOffset = hasWGSLHeader ? sizeof(wgslHeader) : 0;
		if (data[0].Size <= static_cast<int32_t>(codeOffset))
		{
			return false;
		}

		wgslCode.assign(reinterpret_cast<const char*>(bytes + codeOffset), static_cast<size_t>(data[0].Size) - codeOffset);
		while (!wgslCode.empty() && wgslCode.back() == '\0')
		{
			wgslCode.pop_back();
		}
		wgslDesc.code = wgpu::StringView(wgslCode.data(), wgslCode.size());
		desc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&wgslDesc);
	}
	else
	{
		sprivDesc.codeSize = data[0].Size / sizeof(uint32_t);
		sprivDesc.code = reinterpret_cast<const uint32_t*>(data[0].Data);
		desc.nextInChain = reinterpret_cast<wgpu::ChainedStruct*>(&sprivDesc);
	}

	shaderModule_ = device.CreateShaderModule(&desc);

	return shaderModule_ != nullptr;
}

wgpu::ShaderModule& ShaderWebGPU::GetShaderModule() { return shaderModule_; }

} // namespace LLGI
