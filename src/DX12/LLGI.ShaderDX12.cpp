
#include "LLGI.ShaderDX12.h"
#include "../LLGI.Shader.h"
#include "LLGI.CompilerDX12.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace LLGI
{

bool ShaderDX12::Initialize(DataStructure* data, int32_t count)
{
	if (data == nullptr || count == 0)
	{
		return false;
	}

	auto p = static_cast<const uint8_t*>(data->Data);
	data_.resize(data->Size);
	memcpy(data_.data(), p, data_.size());

	ID3D12ShaderReflection* reflection = nullptr;
	auto hr = D3DReflect(data_.data(), data_.size(), IID_PPV_ARGS(&reflection));
	if (SUCCEEDED(hr) && reflection != nullptr)
	{
		D3D12_SHADER_DESC shaderDesc = {};
		if (SUCCEEDED(reflection->GetDesc(&shaderDesc)))
		{
			for (uint32_t i = 0; i < shaderDesc.BoundResources; i++)
			{
				D3D12_SHADER_INPUT_BIND_DESC bindDesc = {};
				if (FAILED(reflection->GetResourceBindingDesc(i, &bindDesc)))
				{
					continue;
				}

				if (bindDesc.BindPoint >= NumComputeBuffer)
				{
					continue;
				}

				if (bindDesc.Type == D3D_SIT_BYTEADDRESS)
				{
					byteAddressSRVs_[bindDesc.BindPoint] = true;
				}
				else if (bindDesc.Type == D3D_SIT_UAV_RWBYTEADDRESS)
				{
					byteAddressUAVs_[bindDesc.BindPoint] = true;
				}
			}
		}
	}
	SafeRelease(reflection);

	return true;
}

} // namespace LLGI
