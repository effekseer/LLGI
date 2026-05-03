#include "LLGI.ShaderWebGPU.h"
#include <cctype>
#include <cstring>
#include <string>

namespace LLGI
{
namespace
{
bool ParseAttributeIndex(const std::string& code, const char* attribute, size_t offset, uint32_t& value)
{
	const auto attributePos = code.find(attribute, offset);
	if (attributePos == std::string::npos)
	{
		return false;
	}

	const auto openPos = code.find('(', attributePos);
	if (openPos == std::string::npos)
	{
		return false;
	}

	auto digitPos = openPos + 1;
	while (digitPos < code.size() && std::isspace(static_cast<unsigned char>(code[digitPos])) != 0)
	{
		digitPos++;
	}

	if (digitPos >= code.size() || std::isdigit(static_cast<unsigned char>(code[digitPos])) == 0)
	{
		return false;
	}

	uint32_t parsed = 0;
	while (digitPos < code.size() && std::isdigit(static_cast<unsigned char>(code[digitPos])) != 0)
	{
		parsed = parsed * 10 + static_cast<uint32_t>(code[digitPos] - '0');
		digitPos++;
	}

	value = parsed;
	return true;
}

std::vector<ShaderBindingWebGPU> ReflectBindings(const std::string& code)
{
	std::vector<ShaderBindingWebGPU> bindings;

	size_t offset = 0;
	while (true)
	{
		const auto groupPos = code.find("@group", offset);
		if (groupPos == std::string::npos)
		{
			break;
		}

		uint32_t group = 0;
		uint32_t binding = 0;
		if (ParseAttributeIndex(code, "@group", groupPos, group) &&
			ParseAttributeIndex(code, "@binding", groupPos, binding))
		{
			const auto statementEnd = code.find(';', groupPos);
			const auto statementLength = statementEnd == std::string::npos ? std::string::npos : statementEnd - groupPos;
			const auto statement = code.substr(groupPos, statementLength);
			ShaderBindingResourceTypeWebGPU resourceType = ShaderBindingResourceTypeWebGPU::Unknown;
			if (statement.find("var<uniform>") != std::string::npos)
			{
				resourceType = ShaderBindingResourceTypeWebGPU::UniformBuffer;
			}
			else if (statement.find("var<storage") != std::string::npos)
			{
				resourceType = ShaderBindingResourceTypeWebGPU::StorageBuffer;
			}
			else if (statement.find(": texture_") != std::string::npos)
			{
				resourceType = ShaderBindingResourceTypeWebGPU::Texture;
			}
			else if (statement.find(": sampler") != std::string::npos)
			{
				resourceType = ShaderBindingResourceTypeWebGPU::Sampler;
			}

			bool exists = false;
			for (const auto& b : bindings)
			{
				if (b.Group == group && b.Binding == binding && b.ResourceType == resourceType)
				{
					exists = true;
					break;
				}
			}

			if (!exists)
			{
				ShaderBindingWebGPU reflected;
				reflected.Group = group;
				reflected.Binding = binding;
				reflected.ResourceType = resourceType;
				bindings.push_back(reflected);
			}
		}

		offset = groupPos + 6;
	}

	return bindings;
}
} // namespace

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
		bindings_ = ReflectBindings(wgslCode);
		hasBindingReflection_ = true;
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

const std::vector<ShaderBindingWebGPU>& ShaderWebGPU::GetBindings() const { return bindings_; }

bool ShaderWebGPU::HasBindingReflection() const { return hasBindingReflection_; }

} // namespace LLGI
